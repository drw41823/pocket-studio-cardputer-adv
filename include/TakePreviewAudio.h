#pragma once
#include "AudioStream.h"
namespace pocket {
// AudioStream's existing consumer runs at 48 kHz. The sampler output remains
// 24 kHz: consume pairs and average them, without changing hardware clocks.
__attribute__((noinline)) inline void renderTakeStream(AudioStream& stream,int16_t* out,unsigned count){
    int16_t pairs[256];
    while(count){unsigned n=std::min(128u,count);stream.render(pairs,n*2);
        for(unsigned i=0;i<n;++i)out[i]=int16_t((int(pairs[i*2])+int(pairs[i*2+1]))/2);
        out+=n;count-=n;
    }
}
}
