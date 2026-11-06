#pragma once
#include "SampleRegion.h"
namespace pocket {
inline unsigned takeTrimStep(unsigned frames,unsigned rate,unsigned heldMs,bool fine,bool coarse,bool seconds){
    if(fine)return 1;
    if(seconds)return std::max(1u,rate);
    unsigned step=std::max(std::max(1u,rate/100),unsigned((uint64_t(frames)+231)/232));
    unsigned factor=coarse?10:heldMs>=1600?20:heldMs>=650?5:1;
    return unsigned(std::min<uint64_t>(frames,uint64_t(step)*factor));
}
inline void moveTakeTrim(unsigned frames,unsigned field,int64_t delta,unsigned& start,unsigned& end){
    if(frames<32)return;
    start=std::min(start,frames-32);end=std::max(start+32,std::min(end,frames));
    if(field==0)start=unsigned(std::max<int64_t>(0,std::min<int64_t>(end-32,int64_t(start)+delta)));
    else if(field==1)end=unsigned(std::max<int64_t>(start+32,std::min<int64_t>(frames,int64_t(end)+delta)));
    else {unsigned width=end-start;start=unsigned(std::max<int64_t>(0,std::min<int64_t>(frames-width,int64_t(start)+delta)));end=start+width;}
}
struct TakeWave : SampleWaveform {
    unsigned frames=0,done=0;bool seen[Bins]{};
    void begin(unsigned total){frames=total;done=peak=0;std::fill(low,low+Bins,0);std::fill(high,high+Bins,0);std::fill(seen,seen+Bins,false);}
    void append(int16_t value){
        if(done>=frames)return;unsigned bin=unsigned(uint64_t(done)*Bins/frames);
        if(!seen[bin]){low[bin]=high[bin]=value;seen[bin]=true;}else {low[bin]=std::min(low[bin],value);high[bin]=std::max(high[bin],value);}
        peak=std::max(peak,unsigned(std::abs(int(value))));++done;
    }
};
}
