//
// Created by 高涵 on 2020/4/9.
//
#pragma once

#include <iostream>
#include <deque>
#include <mutex>
#include <thread>

extern "C" {
#include "libavformat/avformat.h"
}

using namespace std;

class PacketGrabber{
    const string inputUrl;
    bool stream_eof = false;
    AVFormatContext	*avFormatCtx = nullptr;

public:

    PacketGrabber(const string& url)
        :inputUrl(url){
        avFormatCtx = avformat_alloc_context();
    }

    int start(){
        if(avformat_open_input(&avFormatCtx, inputUrl.c_str(), NULL, NULL) != 0){
            printf("Couldn't open input stream.\n");
            return -1;
        }
        if(avformat_find_stream_info(avFormatCtx,NULL) < 0){
            printf("Couldn't find stream information.\n");
            return -1;
        }

        return 0;
    }

    int grabPacket(AVPacket *packet){
        if (av_read_frame(avFormatCtx, packet) >= 0) {
//        cout << "read a packet" << endl;
        }else{
            cout << "stream ends." << endl;
            stream_eof = true;
            return -1;
        }
        return 0;
    }
    void release(){
        avformat_close_input(&avFormatCtx);
    }
    bool is_end(){
        return stream_eof;
    }
    AVFormatContext	*get_ctx(){
        return avFormatCtx;
    }


};

