//
// Created by 高涵 on 2020/4/9.
//

#include <AudioProcessor.h>
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libswresample/swresample.h"
#include "SDL2/SDL.h"
}


static  Uint8  *audio_chunk;
static  Uint32  audio_len;
static  Uint8  *audio_pos;

void  read_audio_data(void *userdata, Uint8 *stream, int len){
    SDL_memset(stream, 0, len);
    if(audio_len == 0)		/*  Only  play  if  we  have  data  left  */
        return;
    len = len>audio_len?audio_len:len;	/*  Mix  as  much  data  as  possible  */

    SDL_MixAudio(stream,audio_pos,len,SDL_MIX_MAXVOLUME);
    audio_pos += len;
    audio_len -= len;
}


int playAudio(AudioProcessor* a_pro){

    AVCodecContext *audioCodecCtx = a_pro->get_codec_ctx();
    auto audio_info = a_pro->audio_info;

    SDL_AudioSpec wanted_spec;
    wanted_spec.freq = audio_info.sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = audio_info.channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = audio_info.nb_samples;
    wanted_spec.callback = read_audio_data;
    wanted_spec.userdata = audioCodecCtx;

    if (SDL_OpenAudio(&wanted_spec, NULL)<0){
        printf("can't open audio.\n");
        return -1;
    }

    cout << "wanted_specs.freq:" << wanted_spec.freq << endl;
    std::printf("wanted_specs.format: Ox%X\n", wanted_spec.format);
    cout << "wanted_specs.channels:" << (int)wanted_spec.channels << endl;
    cout << "wanted_specs.samples:" << (int)wanted_spec.samples << endl;

    cout << "------------------------------------------------" << endl;

    cout << "waiting audio play..." << endl;

    int out_size = av_samples_get_buffer_size(NULL, audio_info.channels, audio_info.nb_samples, audio_info.sample_fmt, 1);

    if(out_size < 0){
        cout << "init audio error" << out_size
        << endl;
        return -1;
    }

    SDL_PauseAudio(0);
    while(!a_pro->get_closed()){
        a_pro->grabAudio();
        uint8_t *buffer = a_pro->out_buffer;
        if(buffer != nullptr){
            while(audio_len > 0)
                SDL_Delay(1);
            audio_chunk = buffer;
            audio_len = out_size;
            audio_pos = audio_chunk;}
    }
    cout << "stop play audio" << endl;
    return 0;
}

//struct AudioUtil{
//    AudioProcessor* grabber;
//    ReSampler* reSampler;
//
//};
//
//void read_audio_data(void *userdata, Uint8 *stream, int len) {
//
//    AudioUtil* audioUtil = (AudioUtil*) userdata;
//    AudioProcessor *grabber = audioUtil->grabber;
//    ReSampler* reSampler = audioUtil -> reSampler;
//
//    static uint8_t *outbuffer = nullptr;
//    static int outBufferSize = 0;
//   static AVFrame *aFrame = av_frame_alloc();
//
//    grabber->grabAudio();
//
//    AVFrame* aFrame = grabber->frame;
//
//  if(ret == 2){
//        if(outbuffer == nullptr){
//            outBufferSize = reSampler-> allocDataBuf(&outbuffer, aFrame->nb_samples);
//            cout <<"------audio samples: "<< aFrame->nb_samples<<endl;
//        } else {
//            memset(outbuffer, 0, outBufferSize);
//        }
//
//        int outSamples;
//        int outDataSize;
//        ////获取数据的siza???
//        std::tie(outSamples, outDataSize) = reSampler->reSample(outbuffer, outBufferSize, aFrame);
//
//        if(outDataSize != len){
//            cout<<"WARNING: outDataSize["<<outDataSize<<"]!= len["<<len<<"]"<<endl;
//        }
//        std::memcpy(stream, outbuffer, outDataSize);
////    }
//
//
//
//}


