#pragma once
#include <cstdint>
namespace pocket {
struct MP3Decoder;
struct MP3Frame {int bytes=0,offset=0,channels=0,rate=0,bitrate=0;};
MP3Decoder* createMP3();void destroyMP3(MP3Decoder*);void resetMP3(MP3Decoder*);
int decodeMP3(MP3Decoder*,const uint8_t*,int,int16_t*,MP3Frame&);
}
