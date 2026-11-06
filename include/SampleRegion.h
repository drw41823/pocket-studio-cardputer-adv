#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
namespace pocket {
inline unsigned samplePeak(const int16_t* data,unsigned count) {
    unsigned peak=0;
    if(data)for(unsigned i=0;i<count;++i)peak=std::max(peak,unsigned(std::abs(int(data[i]))));
    return peak;
}
inline float sampleLevelGain(unsigned peak) {return peak?std::min(32.f,29490.f/peak):1.f;}
// Fixed display cache only: no duplicate PCM or heap allocation.
struct SampleWaveform {
    static constexpr unsigned Bins=116;
    int16_t low[Bins]{},high[Bins]{};
    unsigned peak=0;
    void build(const int16_t* data,unsigned frames,unsigned start,unsigned end) {
        peak=0;std::fill(low,low+Bins,0);std::fill(high,high+Bins,0);
        end=std::min(end,frames);if(!data||start>=end)return;
        unsigned length=end-start;
        for(unsigned x=0;x<Bins;++x) {
            unsigned begin=start+x*length/Bins,finish=std::min(end,std::max(begin+1,start+(x+1)*length/Bins));
            int16_t lo=32767,hi=-32768;
            for(unsigned i=begin;i<finish;++i){lo=std::min(lo,data[i]);hi=std::max(hi,data[i]);}
            low[x]=lo;high[x]=hi;peak=std::max(peak,std::max(unsigned(std::abs(int(lo))),unsigned(std::abs(int(hi)))));
        }
    }
};
}
