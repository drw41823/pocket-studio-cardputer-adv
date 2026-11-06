#pragma once
#include <algorithm>
#include "SampleRegion.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace pocket {
enum class SampleEdit : uint8_t { Normalize, Reverse, RemoveDC, Fade };
inline void editSample(int16_t* data,unsigned count,unsigned rate,SampleEdit operation) {
    if(!data || !count || !rate)return;
    if(operation==SampleEdit::Reverse) {std::reverse(data,data+count);return;}
    if(operation==SampleEdit::Normalize) {
        unsigned peak=samplePeak(data,count);if(!peak)return;
        for(unsigned i=0;i<count;++i)data[i]=int16_t(int64_t(data[i])*29490/peak); // -0.9 dBFS, no hardware gain change
    } else if(operation==SampleEdit::RemoveDC) {
        int64_t total=0;for(unsigned i=0;i<count;++i)total+=data[i];
        int mean=int(total/int64_t(count));
        for(unsigned i=0;i<count;++i)data[i]=int16_t(std::max(-32768,std::min(32767,int(data[i])-mean)));
    } else if(operation==SampleEdit::Fade) {
        unsigned n=std::min(count/2,std::max(1u,rate/200)); // 5 ms on each edge
        for(unsigned i=0;i<n;++i) {
            data[i]=int16_t(int64_t(data[i])*i/n);
            data[count-1-i]=int16_t(int64_t(data[count-1-i])*i/n);
        }
    }
}
inline void wav16Header(uint8_t* header,unsigned frames,unsigned rate) {
    std::fill(header,header+44,0);
    auto put16=[&](unsigned at,unsigned v){header[at]=v;header[at+1]=v>>8;};
    auto put32=[&](unsigned at,uint32_t v){for(unsigned i=0;i<4;++i)header[at+i]=v>>(i*8);};
    std::memcpy(header,"RIFF",4);put32(4,36+frames*2);std::memcpy(header+8,"WAVEfmt ",8);
    put32(16,16);put16(20,1);put16(22,1);put32(24,rate);put32(28,rate*2);
    put16(32,2);put16(34,16);std::memcpy(header+36,"data",4);put32(40,frames*2);
}
}
