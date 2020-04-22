//
// Created by 高涵 on 2020-04-02.
//
#pragma once

#ifdef _WIN32
// Windows
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "libswresample/swresample.h"
};
#else
// Linux...
#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#ifdef __cplusplus
};
#endif
#endif

#include <string>
#include <iostream>
#include <sstream>
#include <tuple>
#include <list>
#include <deque>
#include <mutex>


namespace ffmpegUtil {

    using std::cout;
    using std::endl;
    using std::string;
    using std::stringstream;
    using std::list;
    using std::unique_ptr;
    using std::mutex;
    using std::deque;


    class ReSampler;

    struct ffutils{

        //初始化解码器函数
        static void initCodec(AVFormatContext* formatContext, int streamIndex, AVCodecContext** avCodecContext){
            string codecType{};
            switch (formatContext->streams[streamIndex]->codec->codec_type){
                case AVMEDIA_TYPE_VIDEO:
                    codecType = "video_codec";
                    break;
                case AVMEDIA_TYPE_AUDIO:
                    codecType = "audio_codec";
                    break;
                default:
                    throw std:: runtime_error("error_decodec, unknowtype");
            }

            //根据流的codec 找到相应的解码器
            AVCodec* codec = avcodec_find_decoder(formatContext->streams[streamIndex]->codecpar->codec_id);

            if(codec == nullptr){
                string errMsg = "Could not find codec";
                errMsg += (*avCodecContext)->codec_id;
                cout<<errMsg<<endl;
                throw std::runtime_error(errMsg);
            }

            // Allocate an AVCodecContext and set its fields to default values.
            (*avCodecContext) = avcodec_alloc_context3(codec);
            auto codecCtx = *avCodecContext;

            //将媒体流中的编码器参数设置到codecCtx中
            if(avcodec_parameters_to_context(codecCtx, formatContext->streams[streamIndex]->codecpar) != 0){
                string errorMsg = "Could not copy codec context";
                errorMsg += codec->name;
                cout<<errorMsg<<endl;
                throw std::runtime_error(errorMsg);
            }

            //初始化编码器
            if(avcodec_open2(codecCtx, codec, nullptr) < 0){
                string errorMsg = "Could not open codec: ";
                errorMsg += codec->name;
                cout << errorMsg << endl;
                throw std::runtime_error(errorMsg);
            }

            cout<< codecType<< "[" << codecCtx->codec->name<<"] codec context initialize success"<<endl;

        }

    };

class PacketGrabber{
    const string inputUrl;
    AVFormatContext* formatCtx  = nullptr;
    bool streamEnd = false;

public:PacketGrabber(const string& url)
            :inputUrl(url){
        formatCtx = avformat_alloc_context();
    }

    void init(){
        if(avformat_open_input(&formatCtx, inputUrl.c_str(), NULL, NULL) != 0){
            string errorMsg = "Can not open input file:";
            errorMsg += inputUrl;
            cout << errorMsg << endl;
            throw std::runtime_error(errorMsg);
        }

        if(avformat_find_stream_info(formatCtx, NULL) <0){
            string errorMsg = "Can not find stream information in input file:";
            errorMsg += inputUrl;
            cout << errorMsg << endl;
            throw std::runtime_error(errorMsg);
        }
    }

    int grabPkt(AVPacket* packet){
        if (av_read_frame(formatCtx, packet) >= 0) {
//            cout << "read a packet" << endl;
        }else{
            cout << "stream ends." << endl;
            streamEnd = true;
            return -1;
        }
        return 0;
    }

    AVFormatContext* get_ctx(){
        return formatCtx;
    }

    bool isEnd(){
        return streamEnd;
    }

};




class FrameGrabber{
    const string inputUrl;


    int videoIndex = -1;
    int audioIndex = -1;

    const bool videoEnabled;
    const bool audioEnabled;

    string videoCodecName = "unknown";
    AVFormatContext* formatCtx  = nullptr;
    AVCodecContext* vCodecCtx = nullptr;
    AVCodecContext* aCodecCtx = nullptr;
    AVPacket* packet = (AVPacket*) av_malloc(sizeof(AVPacket));
    bool isEnd = false;


    deque<AVPacket> packet_que = {};
    mutex packet_que_mutex{};

    int64_t clock = 0;
    mutex clock_mutex;


protected:


    AVRational streamTimeBase{1, 0};

    int grabFrameByType(AVFrame* frame, AVMediaType streamType){
        int ret = -1;
        int targetStream;

        switch (streamType){
            case AVMEDIA_TYPE_VIDEO:
                targetStream = videoIndex;
                break;
            case AVMEDIA_TYPE_AUDIO:
                targetStream = audioIndex;
                break;
            default:
                targetStream = -1;
                break;
        }

        while(true){
            int currentPacketStreamIndex = -1;
//            while(!isEnd){
   //             if(av_read_frame(formatCtx, packet) >= 0){
                    get_packet(packet);
                    currentPacketStreamIndex = packet->stream_index;

                    if(packet->stream_index == videoIndex && videoEnabled){ //处理视频
                        //feed video packet to codec
                        ret = avcodec_send_packet(vCodecCtx, packet);
                        printf("-----------%d\n", ret);

                        if(ret == 0){
                            av_packet_unref(packet);
                            break;
                        } else {
                            string errorMsg = "[VIDEO] avcodec_send_packet error: ";
                            errorMsg += ret;
                            cout << errorMsg << endl;
                            throw std::runtime_error(errorMsg);
                        }


                    } else if(packet->stream_index == audioIndex && audioEnabled){//处理音频

                        if (packet->pts != AV_NOPTS_VALUE) {
                            set_pts(packet->pts * av_q2d(streamTimeBase) * 1000 );
                        }
                        set_pts(clock + 1000 * packet->size/((double) 44100 * 2 * 2));

                        ret = avcodec_send_packet(aCodecCtx, packet);
                        printf("================%d\n", ret);

                        if(ret == 0){
                           av_packet_unref(packet);
                            break;
                        } else {
                            string errorMsg = "[AUDIO] avcodec_send_packet error: ";
                            errorMsg += ret;
                            cout << errorMsg << endl;
                            throw std::runtime_error(errorMsg);
                        }

                    } else {
                        stringstream ss{};
                        ss<<"av_read_frame skip packe in streanIndex ="<<currentPacketStreamIndex;
                        av_packet_unref(packet);   // 丢弃被跳过的packet
                    }
            //}

            ret = -1;

            if(currentPacketStreamIndex == videoIndex && videoEnabled){
                ret = avcodec_receive_frame(vCodecCtx, frame);
                if(frame->best_effort_timestamp != AV_NOPTS_VALUE){
                    set_pts(frame->pts * av_q2d(streamTimeBase) * 1000);
                }
            } else if(currentPacketStreamIndex == audioIndex && audioEnabled){
                ret = avcodec_receive_frame(aCodecCtx, frame);

//                if(frame->best_effort_timestamp != AV_NOPTS_VALUE){
//                    set_pts(frame->pts * av_q2d(streamTimeBase) * 1000);
//                }
            } else{
                if(isEnd){
                    cout << "no more frames." << endl;
                    return 0;
                } else {
                    stringstream ss{};
                    ss << "unknown situation: currentPacketStreamIndex:" << currentPacketStreamIndex;
                    continue;
                }
            }

            if(ret == 0){
                if(targetStream = -1 || currentPacketStreamIndex == targetStream){
                    stringstream ss{};
                    ss << "avcodec_receive_frame ret == 0. got [" << targetStream << "] ";

                    if (currentPacketStreamIndex == videoIndex) {
                        return 1;
                    } else if (currentPacketStreamIndex == audioIndex) {
                        return 2;
                    } else {
                        string errorMsg = "Unknown situation, it should never happen. ";
                        errorMsg += ret;
                        cout << errorMsg << endl;
                        throw std::runtime_error(errorMsg);
                    }
                } else {
                    continue;
                }

            } else if(ret == AVERROR(EAGAIN)){
                cout << "need more packet." << endl;
            } else if(ret == AVERROR_EOF){
                cout << "+++++++++++++no more output frames. index="
                     << packet->stream_index<< endl;
            }else {
                string errorMsg = "avcodec_receive_frame error: ";
                errorMsg += ret;
                cout << errorMsg << endl;
                throw std::runtime_error(errorMsg);
            }

//            av_frame_free(&frame);
//            av_packet_free(&packet);
        }

    }

public:
    FrameGrabber(const string& url, bool enableVideo = true, bool enableAudio = true)
        :inputUrl(url), videoEnabled(enableVideo), audioEnabled(enableAudio){
        formatCtx = avformat_alloc_context();
    }
    void start(){
        if(avformat_open_input(&formatCtx, inputUrl.c_str(), NULL, NULL) != 0){
            string errorMsg = "Can not open input file:";
            errorMsg += inputUrl;
            cout << errorMsg << endl;
            throw std::runtime_error(errorMsg);
        }

        if(avformat_find_stream_info(formatCtx, NULL) <0){
            string errorMsg = "Can not find stream information in input file:";
            errorMsg += inputUrl;
            cout << errorMsg << endl;
            throw std::runtime_error(errorMsg);
        }

        for(int i = 0; i < formatCtx->nb_streams; i++){
            if(formatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && videoIndex == -1){
                videoIndex = i;
                streamTimeBase = formatCtx->streams[i]->time_base;
                cout << "video stream index = : [" << i<< "]" << endl;
            }
            if(formatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && audioIndex == -1){
                audioIndex = i;
                streamTimeBase = formatCtx->streams[i]->time_base;
                cout << "audio stream index = : [" << i << "]" << endl;
            }

        }

        if(videoEnabled){
            if(videoIndex == -1){
                string errMsg = "Don't find a video stream in the file";
                errMsg += inputUrl;
                cout<<errMsg<<endl;
                throw std::runtime_error(errMsg);
            } else {
                ffutils::initCodec(formatCtx, videoIndex, &vCodecCtx);
                cout<<"Init video codec success";
            }
        }

        if(audioEnabled){
            if(audioIndex == -1){
                string errMsg = "Don't find a audio stream in the file";
                errMsg += inputUrl;
                cout<<errMsg<<endl;
                throw std::runtime_error(errMsg);
            } else{
                ffutils::initCodec(formatCtx, audioIndex, &aCodecCtx);
            }
        }

        cout<<"-----------------File Information---------------"<< endl;
        //av_dump_format(formatCtx, videoIndex, inputUrl.c_str(), 0); //Print detailed information about the input or output format
        cout << "-------------------------------------------------\n" << endl;
        packet = (AVPacket*)av_malloc(sizeof(AVPacket));

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

    int64_t get_pts(){
        std::lock_guard<std::mutex> lock(clock_mutex);
        return clock;
    }

    void set_pts(int64_t pts){
        std::lock_guard<std::mutex> lock(clock_mutex);
        clock = pts;
    }
    int get_video_idx(){
        return videoIndex;
    }

    int get_audio_idx(){
        return audioIndex;
    }

    bool fileIsEnd(){
        return isEnd;
    }

    int grabPkt(AVPacket* packet){
        if (av_read_frame(formatCtx, packet) >= 0) {
//            cout << "read a packet" << endl;
        }else{
            cout << "stream ends." << endl;
            isEnd = true;
            return -1;
        }
        return 0;
    }

    void close(){
        // It seems like only one xxx_free_context can be called.
        // Which one should be called?
        avformat_free_context(formatCtx);
        // avcodec_free_context(&pCodecCtx);
        // TODO implement.
    }

    int getWidth() const{
        if(vCodecCtx != nullptr){
            return vCodecCtx->width;
        } else {
            throw std::runtime_error("can not getWidth.");
        }
    }

    int getHeight() const {
        if (vCodecCtx != nullptr) {
            return vCodecCtx->height;
        } else {
            throw std::runtime_error("can not getHeight.");
        }
    }


    double getFrameRate() const {
        if (formatCtx != nullptr) {
            AVRational frame_rate =
                    av_guess_frame_rate(formatCtx, formatCtx->streams[videoIndex], NULL);
            double fr = frame_rate.num && frame_rate.den ? av_q2d(frame_rate) : 0.0;
            return fr;
        } else {
            throw std::runtime_error("can not getFrameRate.");
        }
    }

    int getPixelFormat() const {
         if(vCodecCtx != nullptr){
             return static_cast<int>(vCodecCtx->pix_fmt);
         } else {
             throw std::runtime_error("can not getPixelFormat.");
         }
    }

    int grabImageFrame(AVFrame* pFrame) {
        if (!videoEnabled) {
            throw std::runtime_error("video disabled.");
        }
        int ret = grabFrameByType(pFrame, AVMediaType::AVMEDIA_TYPE_VIDEO);
        return ret;
    }

    int grabAudioFrame(AVFrame* pFrame){
        if(!audioEnabled){
            throw std::runtime_error("audio disabled");
        }
        int ret = grabFrameByType(pFrame, AVMediaType::AVMEDIA_TYPE_AUDIO);
        return ret;
    }

    int getSampleRate() const {
        if(aCodecCtx != nullptr){
            return aCodecCtx->sample_rate;
        } else {
            throw  std::runtime_error("can not get audio sampleRate");
        }
    }
    int getSample() const {
        if(aCodecCtx != nullptr){
            return aCodecCtx->frame_size;
        } else {
            throw  std::runtime_error("can not get audio sampleRate");
        }
    }

    int getChannels() const {
        if(aCodecCtx != nullptr){
            return aCodecCtx->channels;
        }else {
            throw  std::runtime_error("can not get audio channels");
        }
    }

    int getChannelLayout() const{
        if(aCodecCtx != nullptr){
            return aCodecCtx -> channel_layout;
        }else {
            throw  std::runtime_error("can not get audio channelLayout");
        }
    }

    int getSampleFormat() const {
        if(aCodecCtx != nullptr){
            return (int)aCodecCtx->sample_fmt;
        } else {
            throw  std::runtime_error("can not get audio sampleFormat");
        }
    }


};

struct AudioInfo{
    int64_t layout;
    int sampleRate;
    int channles;
    AVSampleFormat format;

    AudioInfo(){
        layout = -1;
        sampleRate = -1;
        channles = -1;
        format = AV_SAMPLE_FMT_S16;
    }

    AudioInfo(int64_t l, int sRate, int ch, AVSampleFormat fmt)
            : layout(l), sampleRate(sRate), channles(ch), format(fmt){}

};

//重采样，改变音频的采样率等参数，使得音频按照我们期望的参数输出
class ReSampler{
    SwrContext* swr; // 重采样结构体，改变音频的采样率等参数

public:
    ReSampler(const ReSampler&) = delete;
    ReSampler(ReSampler&&) noexcept = delete;
    ReSampler operator = (const ReSampler&) = delete;
public:~ReSampler(){
        cout<<"~ReSampler called"<<endl;
        if(swr != nullptr){
            swr_free(&swr);
        }
    }

    const AudioInfo in;
    const AudioInfo out;

public:

    static AudioInfo getDefaultAudioInfo(int sr) {
        int64_t layout = AV_CH_LAYOUT_STEREO;
        int sampleRate = sr;
        int channels = 2;
        AVSampleFormat format = AV_SAMPLE_FMT_S16;

        return ffmpegUtil::AudioInfo(layout, sampleRate, channels, format);
    }

public:ReSampler(AudioInfo input, AudioInfo output): in(input), out(output){
        swr = swr_alloc_set_opts(nullptr, out.layout, out.format, out.sampleRate,
                in.layout, in.format, in.sampleRate, 0, nullptr);
        if(swr_init(swr)){
            throw  std::runtime_error("swr_init error");
        }

    }

public:int allocDataBuf(uint8_t** outData, int inputSample){
        int bytePerOutSample = -1;
        switch (out.format){
            case AV_SAMPLE_FMT_U8:
                bytePerOutSample = 1;
                break;
            case AV_SAMPLE_FMT_S16P:
            case AV_SAMPLE_FMT_S16:
                bytePerOutSample = 2;
                break;
            case AV_SAMPLE_FMT_S32:
            case AV_SAMPLE_FMT_S32P:
            case AV_SAMPLE_FMT_FLT:
            case AV_SAMPLE_FMT_FLTP:
                bytePerOutSample = 4;
                break;
            case AV_SAMPLE_FMT_DBL:
            case AV_SAMPLE_FMT_DBLP:
            case AV_SAMPLE_FMT_S64:
            case AV_SAMPLE_FMT_S64P:
                bytePerOutSample = 8;
                break;
            default:
                bytePerOutSample = 2;
                break;
        }

        int outSamplesPerChannel = av_rescale_rnd(inputSample, out.sampleRate, in.sampleRate, AV_ROUND_UP);

        int outSize = outSamplesPerChannel * out.channles* bytePerOutSample;

        std::cout << "GuessOutSamplesPerChannel: " << outSamplesPerChannel << std::endl;
        std::cout << "GuessOutSize: " << outSize << std::endl;

        outSize *= 1.2;
        *outData = (uint8_t*)av_malloc(sizeof(uint8_t)* outSize);


        return outSize;
    }

public: std::tuple<int,int> reSample(uint8_t* dataBuffer, int dataBufferSize, const AVFrame* aframe){

        int outSample = swr_convert(swr, &dataBuffer, dataBufferSize, (const uint8_t**)&aframe->data[0], aframe->nb_samples);

        if (outSample <= 0){
            throw std::runtime_error("error: outSample=");
        }

        int outDataSize = av_samples_get_buffer_size(NULL, out.channles, outSample, out.format, 1);

        if(outDataSize <= 0){
            throw  std::runtime_error("error: outDataSize=");
        }
        return {outSample,outDataSize};
    }



};


}