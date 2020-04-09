//
// Created by 高涵 on 2020-04-07.
//
#include <iostream>
#include <fstream>
#include "ffmpegUtil.h"

extern "C" {
#include "SDL2/SDL.h"
};


using std::cout;
using std::endl;
using std::string;

namespace {
    using namespace ffmpegUtil;

    struct AudioUtil{
        FrameGrabber* grabber;
        ReSampler* reSampler;

    };


    void read_audio_data(void *userdata, Uint8 *stream, int len) {
        AudioUtil* audioUtil = (AudioUtil*) userdata; ///?
        FrameGrabber *grabber = audioUtil->grabber;
        ReSampler* reSampler = audioUtil -> reSampler;

        static uint8_t *outbuffer = nullptr;
        static int outBufferSize = 0;
        static AVFrame *aFrame = av_frame_alloc();

        int ret = grabber->grabAudioFrame(aFrame);

        if(ret == 2){
            if(outbuffer == nullptr){
                outBufferSize = reSampler-> allocDataBuf(&outbuffer, aFrame->nb_samples);
                cout <<"------audio samples: "<< aFrame->nb_samples<<endl;
            } else {
                memset(outbuffer, 0, outBufferSize);
            }

            int outSamples;
            int outDataSize;
            ////获取数据的siza???
            std::tie(outSamples, outDataSize) = reSampler->reSample(outbuffer, outBufferSize, aFrame);

            if(outDataSize != len){
                cout<<"WARNING: outDataSize["<<outDataSize<<"]!= len["<<len<<"]"<<endl;
            }
            std::memcpy(stream, outbuffer, outDataSize);
        }



    }


    void playMediaAudio(const string &inputPath) {
        FrameGrabber grabber{inputPath, false, true};
        grabber.start();

        int64_t inlayOut = grabber.getChannelLayout();
        int insampleRate = grabber.getSampleRate();
        int inchannels = grabber.getChannels();

        AVSampleFormat inFormat = AVSampleFormat(grabber.getSampleFormat());

        AudioInfo inAudio(inlayOut, insampleRate, inchannels, inFormat);
        AudioInfo outAudio = ReSampler::getDefaultAudioInfo(insampleRate);

        outAudio.sampleRate = inAudio.sampleRate;

        ReSampler reSampler(inAudio, outAudio);

        AudioUtil audioUtil{&grabber, &reSampler};

        SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE", "1", 1);

        if(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER)){
            string errMsg = "Can't init SDL";
            errMsg += SDL_GetError();
            cout<<errMsg<<endl;
            throw std::runtime_error(errMsg);
        }

        SDL_AudioSpec wanted_spec;
        SDL_AudioSpec spec;

        cout << "grabber.getSampleFormat() = " << grabber.getSampleFormat() << endl;
        cout << "grabber.getSampleRate() = " << grabber.getSampleRate() << endl;
        cout << "++" << endl;

        wanted_spec.freq = grabber.getSampleRate();
        wanted_spec.format = AUDIO_S16SYS;
        wanted_spec.channels = grabber.getChannels();
        wanted_spec.samples = 1024;
        wanted_spec.callback = read_audio_data;
        wanted_spec.userdata = &audioUtil;

        SDL_AudioDeviceID  audioDeviceId;
        audioDeviceId = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &spec, 0);

        if(audioDeviceId == 0){
            string errMsg = "Failed to open audio device:";
            errMsg += SDL_GetError();
            cout << errMsg << endl;
            throw std::runtime_error(errMsg);
        }

        cout << "wanted_specs.freq:" << wanted_spec.freq << endl;
        // cout << "wanted_specs.format:" << wanted_specs.format << endl;
        std::printf("wanted_specs.format: Ox%X\n", wanted_spec.format);
        cout << "wanted_specs.channels:" << (int)wanted_spec.channels << endl;
        cout << "wanted_specs.samples:" << (int)wanted_spec.samples << endl;

        cout << "------------------------------------------------" << endl;

        cout << "specs.freq:" << spec.freq << endl;
        // cout << "specs.format:" << specs.format << endl;
        std::printf("specs.format: Ox%X\n", spec.format);
        cout << "specs.channels:" << (int)spec.channels << endl;
        cout << "specs.silence:" << (int)spec.silence << endl;
        cout << "specs.samples:" << (int)spec.samples << endl;

        cout << "waiting audio play..." << endl;

        SDL_PauseAudioDevice(audioDeviceId, 0);
        SDL_Delay(300000);
        SDL_CloseAudio();

    }

    void playVideo(const string& inputfile){
        cout << "play video: " << inputfile << endl;
        try {
            // playYuvFile(inputPath);
            playMediaAudio(inputfile);
        } catch (std::exception ex) {
            cout << "exception: " << ex.what() << endl;
        }

    }
}
int main() {
    cout << "simple player" << endl;
    string inputFile = "/Users/gaohan/Desktop/out1.mp4";
    playVideo(inputFile);
    return 0;
}