//
// Created by 高涵 on 2020-04-13.
//
#include <iostream>
#include <fstream>
#include<cstdio>
//#include "ffmpegutil.h"

using std::cout;
using std::endl;
using std::string;

//using namespace ffmpegUtil;

extern void playVideo(const string& inputfile);
extern void playAudio(const string&);
extern void playVideoWithAudio(const string&);

int main(){
    cout << "simple player" << endl;
    string inputFile = "/Users/gaohan/Desktop/test.mp4";
    //playAudio(inputFile);
    //playVideo(inputFile);
    playVideoWithAudio(inputFile);
    return 0;

}