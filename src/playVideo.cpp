//
// Created by 高涵 on 2020/4/19.
//


#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <VideoProcessor.h>
#include <AudioProcessor.h>
#include <fstream>
#include <time.h>

#define REFRESH_EVENT (SDL_USEREVENT + 1)

#define BREAK_EVENT (SDL_USEREVENT + 2)

extern "C"{
#include "SDL2/SDL.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
}

int thread_exit = 0;

int time_interval = 0;

void push_refresh_event(){
    SDL_Event event;
    event.type = REFRESH_EVENT;
    SDL_PushEvent(&event);
}

int refreshPicture(void* opaque){
    while(!thread_exit){
        push_refresh_event();
        auto temp_interval = time_interval > 20 ? time_interval : 20;
        temp_interval = temp_interval < 60 ? temp_interval : 60;
        SDL_Delay(temp_interval);
    }
    thread_exit=0;
    SDL_Event event;
    event.type = BREAK_EVENT;
    SDL_PushEvent(&event);
    return 0;
}

int playVideo(VideoProcessor* v_pro, AudioProcessor* a_pro){

    int screen_w = v_pro->getWidth();
    int screen_h = v_pro->getHeight();

    SDL_Window *screen = SDL_CreateWindow("simple player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              screen_w, screen_h,SDL_WINDOW_SHOWN);

    if(!screen) {
        string errMsg = "SDL: could not create window - exiting:";
        errMsg += SDL_GetError();
        cout << errMsg << endl;
        throw std::runtime_error(errMsg);
    }

    SDL_Renderer *sdlRenderer = SDL_CreateRenderer(screen, -1, 0);

    SDL_Texture *sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, screen_w, screen_h);

    time_interval = 1000/v_pro->get_frame_rate();

    SDL_Thread* video_refresh= SDL_CreateThread(refreshPicture, "video_refresh", NULL);

    try {
        SDL_Event event;
        while (true) {
            SDL_WaitEvent(&event);
            if (event.type == REFRESH_EVENT) {
                if (v_pro->get_closed()) {
                    SDL_Event event;
                    event.type = BREAK_EVENT;
                    SDL_PushEvent(&event);
                }
                v_pro->grabImage();

                // 同步判断
                auto vPts = v_pro->get_pts();
                auto aPts = a_pro->get_pts();
                cout << "video_pts==========" << vPts << endl;
                cout << "audio_pts----------" << aPts << endl;
                auto diff = v_pro->get_pts() - a_pro->get_pts();
                if (vPts > aPts && vPts - aPts > 30) {
                    cout << "video faster diff is " << diff << endl;
                    time_interval = time_interval + 20;
                } else if (aPts > vPts && aPts - vPts > 30) {
                    time_interval = 1000 / v_pro->get_frame_rate();
                    cout << "video slow diff is" << diff << endl;
                    push_refresh_event();
                }
                AVFrame *frame = v_pro->frame;

                if (frame->width != 0) {
                    SDL_UpdateYUVTexture(sdlTexture, NULL,
                                         frame->data[0],
                                         frame->linesize[0],
                                         frame->data[1],
                                         frame->linesize[1],
                                         frame->data[2],
                                         frame->linesize[2]);
                    SDL_RenderClear(sdlRenderer);
                    SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
                    SDL_RenderPresent(sdlRenderer);
                }
            } else if (event.type == SDL_QUIT) {
                thread_exit = 1;
            } else if (event.type == BREAK_EVENT) {
                break;
            } else {
                continue;
            }
        }
        cout << "video finished" << endl;
        SDL_CloseAudio();
        SDL_Quit();
        return 0;
    }catch (std::exception ex){
        cout<<"Exception in play video"<< ex.what()<<endl;
    } catch(...){
        cout << "Unknown exception in play media" << endl;
    }
}




