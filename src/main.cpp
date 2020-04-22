//
// Created by 高涵 on 2020/4/.
//

#include "PacketGrabber.h"
#include "VideoProcessor.h"
#include "AudioProcessor.h"
#include <thread>

extern "C" {
#include "SDL2/SDL.h"
};

extern int playVideo(VideoProcessor* v_pro, AudioProcessor* a_pro);
extern int playAudio(AudioProcessor* a_pro);

int readPacket(PacketGrabber *grabber, VideoProcessor *video_p, AudioProcessor *audio_p) {
   int video_idx = video_p->get_stream_idx();
   int audio_idx = audio_p->get_stream_idx();
   auto packet = (AVPacket*)av_malloc(sizeof(AVPacket));
   while(!grabber->is_end()){

       while(video_p->need_new_packet() || audio_p->need_new_packet()){
//           memset(packet, 0, sizeof(AVPacket));
           int ret = grabber->grabPacket(packet);
           if(ret < 0){
               cout << "grab packet fail." << endl;
               break;
           }
//           cout << "packet" << packet->stream_index << endl;
           if(packet->stream_index == video_idx){
               video_p->push_packet(packet);
           }else if(packet->stream_index == audio_idx){
               audio_p->push_packet(packet);
           }else{
//           cout << "wrong packet" << endl;
           }
//           av_packet_unref(packet);
       }
       std::this_thread::sleep_for(std::chrono::milliseconds(10));
   }

   video_p->set_closed();
   audio_p->set_closed();

   av_free(packet);
   return 0;
}



int main(){
    // FFmpeg
    char inputPath[] = "/Users/gaohan/Desktop/test1.mp4";
//    playYuvFile(filepath)

    PacketGrabber packetGrabber{inputPath};
    packetGrabber.start();

    auto formatCtx = packetGrabber.get_ctx();

    VideoProcessor videoProcessor{formatCtx};
    videoProcessor.start();

    AudioProcessor audioProcessor{formatCtx};
    audioProcessor.start();


    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        printf( "Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }

    std::thread read(readPacket, &packetGrabber, &videoProcessor, &audioProcessor);
    std::thread audio(playAudio, &audioProcessor);
    playVideo(&videoProcessor, &audioProcessor);
    read.join();
    audio.join();
    cout << "finish" << endl;
    packetGrabber.release();
    videoProcessor.release();
    audioProcessor.release();
    return 0;
}

