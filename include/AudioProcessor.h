//
// Created by 高涵 on 2020/4/19.
//
#pragma once

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
#define MAX_AUDIO_FRAME_SIZE 192000

using namespace std;

typedef struct AudioInfo{
    uint64_t channel_layout = AV_CH_LAYOUT_STEREO;
    int nb_samples;
    AVSampleFormat sample_fmt = AV_SAMPLE_FMT_S16;
    int sample_rate = 44100;
    int channels;
} AudioInfo;


class AudioProcessor{

    AVFormatContext* avFormatCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    struct SwrContext* au_convert_ctx = nullptr;
    int stream_idx = -1;
    deque<AVPacket> packet_que = {};
    mutex packet_que_mutex{};

    bool closed = false;
    int64_t clock = 0;
    mutex clock_mutex;
    AVRational stream_time_base;

public:
    AudioProcessor(AVFormatContext* format)
    :avFormatCtx(format){
        out_buffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE*2);
        //frame = av_frame_alloc();
    }


    AudioInfo audio_info{};
    uint8_t *out_buffer = nullptr;
    //AVFrame* frame = nullptr;

    int start(){

        if(int ret = open_codec() < 0){
            printf("open codec fail.");
            return -1;
        }

        audio_info.nb_samples = codecCtx->frame_size;
        audio_info.channels = av_get_channel_layout_nb_channels(audio_info.channel_layout);
        int64_t in_channel_layout = av_get_default_channel_layout(codecCtx->channels);

        au_convert_ctx = swr_alloc();

        au_convert_ctx = swr_alloc_set_opts(au_convert_ctx,
                                            audio_info.channel_layout,
                                            audio_info.sample_fmt,
                                            audio_info.sample_rate,
                                            in_channel_layout,
                                            codecCtx->sample_fmt,
                                            codecCtx->sample_rate,0, NULL);

        swr_init(au_convert_ctx);
        stream_time_base = avFormatCtx->streams[stream_idx]->time_base;

        return 0;

    }

    int open_codec(){
        int streamIndex = -1;

        for(int i = 0; i < avFormatCtx->nb_streams; i++){
            if(avFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
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
        codecCtx = avcodec_alloc_context3(avCodec);
        avcodec_parameters_to_context(codecCtx, avCodeParameters);
        if(avcodec_open2(codecCtx, avCodec,NULL)<0){
            printf("Could not open codec.\n");
            return -1;
        }
        stream_idx = streamIndex;

        return 0;

    }
    static void copy_frame(AVFrame* dst, AVFrame* src){
        dst->width = src->width;
        dst->height = src->height;
        dst->format = src->format;
        dst->channels = src->channels;
        dst->channel_layout = src->channel_layout;
        dst->nb_samples = src->nb_samples;
    }
    void grabAudio(){
        AVPacket *packet = av_packet_alloc();
        get_packet(packet);
        if (packet->pts != AV_NOPTS_VALUE) {
//        cout << av_q2d(stream_time_base) << endl;
            set_pts(packet->pts * av_q2d(stream_time_base) * 1000 );
        }

        set_pts(clock + 1000 * packet->size/((double) 44100 * 2 * 2));

        int ret = -1;
        ret = avcodec_send_packet(codecCtx, packet);
        if (ret == 0) {
            // cout << "avcodec_send_packet success." << endl;
        } else if (ret == AVERROR(EAGAIN)) {
            // buff full, can not decode any more, nothing need to do.
            // keep the packet for next time decode.
            av_packet_unref(packet);
        } else if (ret == AVERROR_EOF) {
            // no new packets can be sent to it, it is safe.
            cout << "[WARN]  no new packets can be sent to it. index=" << packet->stream_index << endl;
        } else {
            string errorMsg = "+++++++++ ERROR avcodec_send_packet error audio: ";
            errorMsg += ret;
            cout << errorMsg << endl;
            throw std::runtime_error(errorMsg);
        }

        auto target_frame = av_frame_alloc();
        ret = avcodec_receive_frame(codecCtx, target_frame);

        if (ret == 0) {
           swr_convert(au_convert_ctx, &out_buffer, MAX_AUDIO_FRAME_SIZE, (const uint8_t **)target_frame->data , target_frame->nb_samples);
        }else if (ret == AVERROR_EOF) {
            cout << "+++++++++++++no more output frames. index="
                 << packet->stream_index<< endl;
        } else if (ret == AVERROR(EAGAIN)) {
            // need more packet.
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
        swr_free(&au_convert_ctx);
//        av_free(out_buffer);
        avcodec_free_context(&codecCtx);

        while(!packet_que.empty()){
            std::lock_guard<std::mutex> lock(packet_que_mutex);
            packet_que.pop_front();
        }
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

    AVCodecContext *get_codec_ctx(){
        return codecCtx;
    };

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

//    int getSampleRate() const {
//        if(codecCtx != nullptr){
//            return ccodecCtx->sample_rate;
//        } else {
//            throw  std::runtime_error("can not get audio sampleRate");
//        }
//    }
//    int getSample() const {
//        if(codecCtx != nullptr){
//            return codecCtx->frame_size;
//        } else {
//            throw  std::runtime_error("can not get audio sampleRate");
//        }
//    }
//
//    int getChannels() const {
//        if(codecCtx != nullptr){
//            return codecCtx->channels;
//        }else {
//            throw  std::runtime_error("can not get audio channels");
//        }
//    }
//
//    int getChannelLayout() const{
//        if(codecCtx != nullptr){
//            return codecCtx -> channel_layout;
//        }else {
//            throw  std::runtime_error("can not get audio channelLayout");
//        }
//    }
//
//    int getSampleFormat() const {
//        if(codecCtx != nullptr){
//            return (int)aCodecCtx->sample_fmt;
//        } else {
//            throw  std::runtime_error("can not get audio sampleFormat");
//        }
//    }



};

//
//
//class ReSampler{
//    SwrContext* swr; // 重采样结构体，改变音频的采样率等参数
//
//public:
//    ReSampler(const ReSampler&) = delete;
//    ReSampler(ReSampler&&) noexcept = delete;
//    ReSampler operator = (const ReSampler&) = delete;
//
//public:~ReSampler(){
//        cout<<"~ReSampler called"<<endl;
//        if(swr != nullptr){
//            swr_free(&swr);
//        }
//    }
//
//    const AudioInfo in;
//    const AudioInfo out;
//
//public:
//
//    static AudioInfo getDefaultAudioInfo(int sr) {
//        int64_t layout = AV_CH_LAYOUT_STEREO;
//        int sampleRate = sr;
//        int channels = 2;
//        AVSampleFormat format = AV_SAMPLE_FMT_S16;
//
//        return AudioInfo(layout, sampleRate, channels, format);
//    }
//
//public:ReSampler(AudioInfo input, AudioInfo output): in(input), out(output){
//        swr = swr_alloc_set_opts(nullptr, out.layout, out.sample_fmt, out.sampleRate,
//                                 in.layout, in.format, in.sampleRate, 0, nullptr);
//        if(swr_init(swr)){
//            throw  std::runtime_error("swr_init error");
//        }
//
//    }
//
//public:int allocDataBuf(uint8_t** outData, int inputSample){
//        int bytePerOutSample = -1;
//        switch (out.format){
//            case AV_SAMPLE_FMT_U8:
//                bytePerOutSample = 1;
//                break;
//            case AV_SAMPLE_FMT_S16P:
//            case AV_SAMPLE_FMT_S16:
//                bytePerOutSample = 2;
//                break;
//            case AV_SAMPLE_FMT_S32:
//            case AV_SAMPLE_FMT_S32P:
//            case AV_SAMPLE_FMT_FLT:
//            case AV_SAMPLE_FMT_FLTP:
//                bytePerOutSample = 4;
//                break;
//            case AV_SAMPLE_FMT_DBL:
//            case AV_SAMPLE_FMT_DBLP:
//            case AV_SAMPLE_FMT_S64:
//            case AV_SAMPLE_FMT_S64P:
//                bytePerOutSample = 8;
//                break;
//            default:
//                bytePerOutSample = 2;
//                break;
//        }
//
//        int outSamplesPerChannel = av_rescale_rnd(inputSample, out.sampleRate, in.sampleRate, AV_ROUND_UP);
//
//        int outSize = outSamplesPerChannel * out.channles* bytePerOutSample;
//
//        std::cout << "GuessOutSamplesPerChannel: " << outSamplesPerChannel << std::endl;
//        std::cout << "GuessOutSize: " << outSize << std::endl;
//
//        outSize *= 1.2;
//        *outData = (uint8_t*)av_malloc(sizeof(uint8_t)* outSize);
//
//
//        return outSize;
//    }
//
//public: std::tuple<int,int> reSample(uint8_t* dataBuffer, int dataBufferSize, const AVFrame* aframe){
//
//        int outSample = swr_convert(swr, &dataBuffer, dataBufferSize, (const uint8_t**)&aframe->data[0], aframe->nb_samples);
//
//        if (outSample <= 0){
//            throw std::runtime_error("error: outSample=");
//        }
//
//        int outDataSize = av_samples_get_buffer_size(NULL, out.channles, outSample, out.format, 1);
//
//        if(outDataSize <= 0){
//            throw  std::runtime_error("error: outDataSize=");
//        }
//        return {outSample,outDataSize};
//    }
//
//
//
//};
