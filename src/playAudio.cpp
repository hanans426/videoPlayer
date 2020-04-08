////
//// Created by 高涵 on 2020-04-07.
////
//#include <iostream>
//#include <fstream>
//#include "ffmpegUtil.h"
//
//extern "C" {
//#include "SDL2/SDL.h"
//};
//
//
//using std::cout;
//using std::endl;
//using std::string;
//
//namespace {
//    using namespace ffmpegUtil;
//}
//
//void read_audio_data(void* userdata, Uint8* stream, int len){
//
//    FrameGrabber* grabber =
//
//    static uint8_t* outbuffer = nullptr;
//    static int outBufferSize = 0;
//    static AVFrame* avFrame = av_frame_alloc();
//
//    int ret = g
//
//}
//
//
//void playMediaAudio(const string& inputPath){
//    FrameGrabber grabber{inputPath, false, true};
//    grabber.start();
//
//    int64_t layOut = grabber.getChannelLayout();
//    int sampleRate = grabber.getSampleRate();
//    int channels = grabber.getChannels();
//
//    AVSampleFormat inFormat = AVSampleFormat(grabber.getSampleFormat());
//
//
//
//}
