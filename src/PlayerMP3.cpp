#include "PlayerMP3.h"
#include <new>
static void* decoderScratch=nullptr;
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_SCRATCH_STORAGE (*static_cast<mp3dec_scratch_t*>(decoderScratch))
#include "third_party/minimp3/minimp3.h"
namespace pocket {
struct MP3Decoder {mp3dec_t decoder;mp3dec_scratch_t scratch;};
MP3Decoder* createMP3(){auto* d=new(std::nothrow)MP3Decoder{};if(d)mp3dec_init(&d->decoder);return d;}
void destroyMP3(MP3Decoder* d){delete d;}
void resetMP3(MP3Decoder* d){if(d)mp3dec_init(&d->decoder);}
int decodeMP3(MP3Decoder* d,const uint8_t* data,int size,int16_t* pcm,MP3Frame& f){
    if(!d)return 0;decoderScratch=&d->scratch;mp3dec_frame_info_t info{};
    int n=mp3dec_decode_frame(&d->decoder,data,size,pcm,&info);
    f.bytes=info.frame_bytes;f.offset=info.frame_offset;f.channels=info.channels;f.rate=info.hz;f.bitrate=info.bitrate_kbps;return n;
}
}
