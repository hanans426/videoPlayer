//
// Created by 高涵 on 2020/4/19.
//

#include <iostream>
#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
}

using namespace std;

class VideoProcessor{

    int stream_idx = -1;
    AVFormatContext *avFormatCtx = nullptr;
    AVCodecContext *codecCtx = nullptr;
    struct SwsContext *img_convert_ctx = nullptr;

    deque<AVPacket> packet_que = {};
    mutex packet_que_mutex{};
    mutex clock_mutex;

    bool closed = false;
    int64_t clock = 0;

    AVRational stream_time_base{1,0};

public:
    VideoProcessor(AVFormatContext* format)
         :avFormatCtx(format){
        frame = av_frame_alloc();
    };
    int start(){
        int ret = open_codec(&codecCtx);
        if(ret < 0){
            printf("open codec fail.");
            return -1;
        }
        stream_idx = ret;
        img_convert_ctx = sws_getContext(codecCtx->width, codecCtx->height, codecCtx->pix_fmt, codecCtx->width, codecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
        auto *buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P,  codecCtx->width, codecCtx->height, 1));
        av_image_fill_arrays(frame->data, frame->linesize, buffer,
                             AV_PIX_FMT_YUV420P, codecCtx->width, codecCtx->height,1);

        stream_time_base = avFormatCtx->streams[stream_idx]->time_base;

        return 0;
    }
    int open_codec(AVCodecContext **desc){
        int streamIndex = -1;

        for(int i = 0; i < avFormatCtx->nb_streams; i++){
            if(avFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                streamIndex = i;
                break;
            }
        }

        if(streamIndex == -1){
            printf("Didn't find a video or audio stream.\n");
            return -1;
        }

        AVCodecParameters *avCodeParameters = avFormatCtx->streams[streamIndex]->codecpar;
        AVCodec	*avCodec = avcodec_find_decoder(avCodeParameters->codec_id);
        if(avCodec == NULL){
            printf("Codec not found.\n");
            return -1;
        }
        *desc = avcodec_alloc_context3(avCodec);
        avcodec_parameters_to_context(*desc, avCodeParameters);
        if(avcodec_open2(*desc, avCodec,NULL)<0){
            printf("Could not open codec.\n");
            return -1;
        }

        return streamIndex;
    }

    static void copy_frame(AVFrame* dst, AVFrame* src){
        dst->width = src->width;
        dst->height = src->height;
        dst->format = src->format;
        dst->channels = src->channels;
        dst->channel_layout = src->channel_layout;
        dst->nb_samples = src->nb_samples;
    }
    void grabImage(){
        AVPacket *packet = av_packet_alloc();
        get_packet(packet);

        int ret = -1;
        ret = avcodec_send_packet(codecCtx, packet);
        if (ret == 0) {
            // cout << "avcodec_send_packet success." << endl;
            av_packet_unref(packet);
        } else if (ret == AVERROR(EAGAIN)) {
            // buff full, can not decode any more, nothing need to do.
            // keep the packet for next time decode.
            cout << "buf full." << endl;
        } else if (ret == AVERROR_EOF) {
            // no new packets can be sent to it, it is safe.
            cout << "[WARN]  no new packets can be sent to it. index=" << packet->stream_index << endl;
        } else {
            string errorMsg = "+++++++++ ERROR avcodec_send_packet error video: ";
            errorMsg += ret;
            cout << errorMsg << endl;
            throw std::runtime_error(errorMsg);
        }

        auto target_frame = av_frame_alloc();

        ret = avcodec_receive_frame(codecCtx, target_frame);

        if (ret == 0) {
            if(target_frame->best_effort_timestamp != AV_NOPTS_VALUE){
                set_pts(target_frame->best_effort_timestamp * av_q2d(stream_time_base) * 1000);
            }
            copy_frame(frame, target_frame);
            sws_scale(img_convert_ctx, (const unsigned char* const*)target_frame->data, target_frame->linesize, 0, codecCtx->height, frame->data, frame->linesize);
        }else if (ret == AVERROR_EOF) {
            cout << "+++++++++++++no more output frames. index="
                 << packet->stream_index<< endl;
        } else if (ret == AVERROR(EAGAIN)) {
            // need more packet.
            cout << "need more packet." << endl;
        } else {
            string errorMsg = "avcodec_receive_frame error: ";
            errorMsg += ret;
            cout << errorMsg << endl;
            throw std::runtime_error(errorMsg);
        }
        av_frame_free(&target_frame);
        av_packet_free(&packet);

    }
    void release(){
        sws_freeContext(img_convert_ctx);
        av_frame_free(&frame);
        avcodec_free_context(&codecCtx);

        while(!packet_que.empty()){
            std::lock_guard<std::mutex> lock(packet_que_mutex);
            packet_que.pop_front();
        }
    }

    int getWidth(){
        return codecCtx->width;
    }

    int getHeight(){
        return codecCtx->height;
    }

    bool need_new_packet(){
        return packet_que.size() < 3;
    }

    void push_packet(AVPacket *packet){
        std::lock_guard<std::mutex> lock(packet_que_mutex);
        packet_que.push_back(*packet);
    }

    void get_packet(AVPacket *packet){
        std::lock_guard<std::mutex> lock(packet_que_mutex);
        *packet = packet_que.front();
        packet_que.pop_front();
    }

    double get_frame_rate(){
        auto frame_rate = codecCtx->framerate;
        double fr = frame_rate.num && frame_rate.den ? av_q2d(frame_rate) : 25.0;
        return fr;
    }

    int get_stream_idx(){
        return stream_idx;
    }

    void set_closed(){
        closed = true;
    }

    bool get_closed(){
        return closed;
    }

    int64_t get_pts(){
        std::lock_guard<std::mutex> lock(clock_mutex);
        return clock;
    }

    void set_pts(int64_t pts){
        std::lock_guard<std::mutex> lock(clock_mutex);
        clock = pts;
    }

    AVFrame *frame = nullptr;

};

