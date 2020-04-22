//
// Created by 高涵 on 2020-04-13.
//

#include "../include_old/ffmpegUtil.h"
#include<cstdio>

#include <iostream>
#include <string>
#include <list>
#include <memory>
#include <chrono>
#include <thread>
#include <fstream>

#define REFRESH_EVENT (SDL_USEREVENT + 1)

#define BREAK_EVENT (SDL_USEREVENT + 2)

#define VIDEO_FINISH (SDL_USEREVENT + 3)

extern "C" {
#include "SDL2/SDL.h"
};

namespace {
    using namespace ffmpegUtil;

    int thread_exit = 0;
    int samples = -1;

    int timeInterval = 0;

    int readPkt(FrameGrabber& v_grabber, FrameGrabber& a_grabber) {
        int video_idx = v_grabber.get_video_idx();
        int audio_idx = a_grabber.get_audio_idx();
        auto packet = (AVPacket*)av_malloc(sizeof(AVPacket));
        while(!v_grabber.fileIsEnd()) {
            while (v_grabber.need_new_packet() || a_grabber.need_new_packet()) {
                int ret = v_grabber.grabPkt(packet);
                if (ret < 0) {
                    cout << "grab packet fail." << endl;
                    break;
                }
                if (packet->stream_index == video_idx) {
                    v_grabber.push_packet(packet);
                } else if(packet->stream_index == audio_idx){
                    a_grabber.push_packet(packet);

                }else {
                }
                //           av_packet_unref(packet);
           }
            //std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

//        v_grabber->set_closed();
//        a_grabber->set_closed();

//        av_free(packet);
        return 0;
    }

    void push_refresh_event(){
        SDL_Event event;
        event.type = REFRESH_EVENT;
        SDL_PushEvent(&event);
    }


    const int bpp = 12;

    int refreshPicture(void* opaque){
        while (!thread_exit) {
            push_refresh_event();
            auto temp_interval = timeInterval > 20 ? timeInterval : 20;
            temp_interval = temp_interval < 60 ? temp_interval : 60;
            SDL_Delay(temp_interval);
        }
        thread_exit = 0;
        // Break
        SDL_Event event;
        event.type = BREAK_EVENT;
        SDL_PushEvent(&event);

        return 0;

    }

    void playVideo(FrameGrabber& vgrabber, FrameGrabber* agrabber = nullptr){

        const int w = vgrabber.getWidth();
        const int h = vgrabber.getHeight();
        const auto fmt = AVPixelFormat(vgrabber.getPixelFormat());

        SDL_Window* screen;
        screen = SDL_CreateWindow("little video player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);

        if(!screen){
            string errMsg = "SDL: could not create window - exiting:";
            errMsg += SDL_GetError();
            cout << errMsg << endl;
            throw std::runtime_error(errMsg);
        }

        SDL_Rect sdlRect;
        sdlRect.x = 0;
        sdlRect.y = 0;
        sdlRect.w = w;
        sdlRect.h = h;

        SDL_Renderer* sdlRenderer = SDL_CreateRenderer(screen, -1, 0);

        Uint32 pixFmt = SDL_PIXELFORMAT_IYUV;

        SDL_Texture* sdlTexture = SDL_CreateTexture(sdlRenderer, pixFmt, SDL_TEXTUREACCESS_STREAMING, w,h);

        timeInterval = 1000 / (int)vgrabber.getFrameRate();
        cout << "timeInterval: " << timeInterval << endl;
        SDL_Thread *video_refresh = SDL_CreateThread(refreshPicture, "video_refresh", NULL);


        try{
            AVFrame* frame = av_frame_alloc();
            int ret;
            bool videoFinish = false;

            SDL_Event event;

            struct SwsContext* sws_ctx = sws_getContext(w, h, fmt, w, h, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

            int numByte = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, w, h, 32);
            uint8_t* buffer = (uint8_t*) av_malloc(numByte * sizeof(uint8_t));
            AVFrame* pict = av_frame_alloc();
            av_image_fill_arrays(pict->data, pict->linesize, buffer, AV_PIX_FMT_YUV420P, w, h, 32);

            while (true){
                ret = vgrabber.grabImageFrame(frame);
                SDL_WaitEvent(&event);
                if(event.type == REFRESH_EVENT){
//                   if(agrabber != nullptr){
                        auto vPts = vgrabber.get_pts();
                        auto aPts = agrabber->get_pts();
                        printf("========%d\n", vPts);
                        printf("-------=%d\n", aPts);
                        if(vPts > aPts && vPts - aPts > 30){
                            printf("=========video faster than audio=%d\n", vPts-aPts);
                            timeInterval = timeInterval + 20;
                        } else if(aPts > vPts && aPts - vPts > 30){
                            printf("==========video slower than audio=%d\n", aPts- vPts);
                           // timeInterval = timeInterval -10;
                            push_refresh_event();
                        }
//                    }

                    if(!videoFinish){
                        if (ret == 1) {  // success.
                            sws_scale(sws_ctx, (uint8_t const* const*)frame->data, frame->linesize, 0, h,
                                      pict->data, pict->linesize);
                            SDL_UpdateYUVTexture(sdlTexture,
                                                 &sdlRect,
                                                 pict->data[0],
                                                 pict->linesize[0],
                                                 pict->data[1],
                                                 pict->linesize[1],
                                                 pict->data[2],
                                                 pict->linesize[2]);

                            SDL_RenderClear(sdlRenderer);
                            SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);
                            SDL_RenderPresent(sdlRenderer);

                        } else if (ret == 0) {  // no more frame.
                            cout << "VIDEO FINISHED." << endl;
                            videoFinish = true;
                            SDL_Event finishEvent;
                            finishEvent.type = VIDEO_FINISH;
                            SDL_PushEvent(&finishEvent);
                        } else {  // error.
                            string errMsg = "grabImageFrame error.";
                            cout << errMsg << endl;
                            throw std::runtime_error(errMsg);
                        }

                    }else {
                        thread_exit = 1;
                        break;
                    }
                } else if(event.type == SDL_QUIT){
                    thread_exit = 1;
                } else if(event.type == BREAK_EVENT){
                    break;
                }

            }
//            av_frame_free(&frame);

        } catch (std::exception ex){
            cout<<"Exception in play video"<< ex.what()<<endl;
        } catch(...){
            cout << "Unknown exception in play media" << endl;
        }

    }

    struct AudioUtil{
        FrameGrabber* grabber;
        ReSampler* reSampler;

    };


    void read_audio_data(void *userdata, Uint8 *stream, int len) {

        AudioUtil* audioUtil = (AudioUtil*) userdata;
        FrameGrabber *grabber = audioUtil->grabber;
        ReSampler* reSampler = audioUtil -> reSampler;

        static uint8_t *outbuffer = nullptr;
        static int outBufferSize = 0;
        static AVFrame *aFrame = av_frame_alloc();

        int ret = grabber->grabAudioFrame(aFrame);

        if (ret == 2) {
            if (outbuffer == nullptr) {
                outBufferSize = reSampler->allocDataBuf(&outbuffer, aFrame->nb_samples);
                cout << "------audio samples: " << aFrame->nb_samples << endl;
            } else {
                memset(outbuffer, 0, outBufferSize);
            }

            int outSamples;
            int outDataSize;
            //获取数据的siza
            std::tie(outSamples, outDataSize) = reSampler->reSample(outbuffer, outBufferSize, aFrame);
            samples = outSamples;
            if (outDataSize != len) {
                cout << "WARNING: outDataSize[" << outDataSize << "]!= len[" << len << "]" << endl;
            }
            std::memcpy(stream, outbuffer, outDataSize);
        } else if (ret == 0) {
            cout << "AUDIO FINISHED." << endl;
        }


    }

    void playAudio(FrameGrabber& agrabber, SDL_AudioDeviceID& audioDeviceId) {

        int64_t inlayOut = agrabber.getChannelLayout();
        int insampleRate = agrabber.getSampleRate();
        int inchannels = agrabber.getChannels();

        AVSampleFormat inFormat = AVSampleFormat(agrabber.getSampleFormat());

        AudioInfo inAudio(inlayOut, insampleRate, inchannels, inFormat);
        AudioInfo outAudio = ReSampler::getDefaultAudioInfo(insampleRate);

        outAudio.sampleRate = inAudio.sampleRate;

        ReSampler reSampler(inAudio, outAudio);

        AudioUtil audioUtil{&agrabber, &reSampler};

        SDL_AudioSpec wanted_spec;
        SDL_AudioSpec spec;

        cout << "grabber.getSampleFormat() = " << agrabber.getSampleFormat() << endl;
        cout << "grabber.getSampleRate() = " << agrabber.getSampleRate() << endl;
        cout << "++" << endl;


        wanted_spec.freq = agrabber.getSampleRate();
        wanted_spec.format = AUDIO_S16SYS;
        wanted_spec.channels = agrabber.getChannels();
        wanted_spec.samples = agrabber.getSample();
        wanted_spec.callback = read_audio_data;
        wanted_spec.userdata = &audioUtil;

        audioDeviceId = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &spec, 0);

        if(audioDeviceId == 0){
            string errMsg = "Failed to open audio device:";
            errMsg += SDL_GetError();
            cout << errMsg << endl;
            throw std::runtime_error(errMsg);
        }

        cout << "wanted_specs.freq:" << wanted_spec.freq << endl;
        std::printf("wanted_specs.format: Ox%X\n", wanted_spec.format);
        cout << "wanted_specs.channels:" << (int)wanted_spec.channels << endl;
        cout << "wanted_specs.samples:" << (int)wanted_spec.samples << endl;

        cout << "------------------------------------------------" << endl;

        cout << "specs.freq:" << spec.freq << endl;
        std::printf("specs.format: Ox%X\n", spec.format);
        cout << "specs.channels:" << (int)spec.channels << endl;
        cout << "specs.silence:" << (int)spec.silence << endl;
        cout << "specs.samples:" << (int)spec.samples << endl;

        cout << "waiting audio play..." << endl;

        SDL_PauseAudioDevice(audioDeviceId, 0);
        SDL_Delay(300000);
        SDL_CloseAudio();

    }

    int play(const string& inputPath){

        PacketGrabber pktGrabber{inputPath};
        pktGrabber.init();

//        FrameGrabber vGrabber{ inputPath, true, false, pktGrabber.get_ctx()};
        FrameGrabber vGrabber{ inputPath, true, false};
        vGrabber.start();

//        FrameGrabber aGrabber{ inputPath, false, true, pktGrabber.get_ctx()};
        FrameGrabber aGrabber{ inputPath, false, true};
        aGrabber.start();



        SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE", "1", 1);

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
            string errMsg = "Could not initialize SDL -";
            errMsg += SDL_GetError();
            cout << errMsg << endl;
            throw std::runtime_error(errMsg);
        }

//        std::thread read(readPkt,std::ref(pktGrabber), std::ref(vGrabber), std::ref(aGrabber));
        std::thread read(readPkt, std::ref(vGrabber), std::ref(aGrabber));

        SDL_AudioDeviceID audioDeviceID;
        std::thread playAudioThread (playAudio, std::ref(aGrabber), std::ref(audioDeviceID));
        playVideo(vGrabber, &aGrabber);
        read.join();

        playAudioThread.join();


        SDL_PauseAudioDevice(audioDeviceID, 1);
        SDL_CloseAudio();

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    }



}

int playVideoWithAudio(const string& filePath){
    play(filePath);
}


