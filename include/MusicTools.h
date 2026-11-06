#pragma once
#include "MusicState.h"
#include <algorithm>
namespace pocket {
struct ToolsSettings {
    uint8_t arp=0, rate=2, gate=50, octaves=1;
    bool chord=false, hold=false;
};
struct PitchSet {
    std::array<uint8_t,8> notes{}; uint8_t count=0;
    bool same(const PitchSet& other) const { return count==other.count && std::equal(notes.begin(),notes.begin()+count,other.notes.begin()); }
};
constexpr uint8_t ScaleIntervals[2][7]={{0,2,4,5,7,9,11},{0,2,3,5,7,8,10}};
inline int degreeOf(unsigned offset,unsigned scale) {
    for(int i=0;i<7;++i) if(ScaleIntervals[scale%2][i]==offset%12) return i;
    return -1;
}
inline PitchSet playedPitches(unsigned root,unsigned scale,unsigned octave,uint32_t keys,const ToolsSettings& settings,unsigned lastKey) {
    PitchSet set;
    if(!keys) return set;
    unsigned base=(octave+1)*12+root;
    if(settings.chord) {
        if(lastKey>=24 || !(keys&(1u<<lastKey))) for(unsigned k=0;k<24;++k) if(keys&(1u<<k)) lastKey=k;
        int degree=degreeOf(lastKey,scale);
        if(degree<0) { set.notes[0]=base+lastKey;set.count=1;return set; }
        unsigned registerBase=base+(lastKey/12)*12;
        for(unsigned i=0;i<3;++i) {
            unsigned d=degree+i*2;set.notes[i]=registerBase+ScaleIntervals[scale%2][d%7]+(d/7)*12;
        }
        if(set.notes[2]>119) for(unsigned i=0;i<3;++i) set.notes[i]-=12;
        set.count=3;
    } else {
        for(unsigned k=0;k<24 && set.count<4;++k) if(keys&(1u<<k)) set.notes[set.count++]=base+k;
    }
    return set;
}
inline unsigned arpRateTicks(const ToolsSettings& settings) { return 96u >> std::min<unsigned>(2,settings.rate); }
inline unsigned arpIndex(unsigned style,unsigned step,unsigned count) {
    if(count<2) return 0;
    if(style==2) return count-1-step%count;
    if(style==3) { unsigned cycle=count*2-2, index=step%cycle;return index<count?index:cycle-index; }
    return step%count;
}
class ArpState {
public:
    void reset() { source={};bank={};started=0;signature=255; }
    PitchSet update(const PitchSet& pressed,const ToolsSettings& settings,int64_t tick) {
        if(!settings.arp) { reset();return pressed; }
        unsigned sig=settings.arp | (settings.rate<<2) | ((settings.octaves-1)<<4) | (settings.hold<<5);
        bool changed=sig!=signature || (pressed.count && !source.same(pressed));
        if(!pressed.count && !settings.hold) { reset();return {}; }
        if(changed && (pressed.count || source.count)) {
            if(pressed.count) source=pressed;
            bank={};
            for(unsigned oct=0;oct<settings.octaves;++oct) for(unsigned i=0;i<source.count;++i) {
                unsigned pitch=source.notes[i]+oct*12;
                if(pitch<=119 && bank.count<bank.notes.size()) bank.notes[bank.count++]=pitch;
            }
            std::sort(bank.notes.begin(),bank.notes.begin()+bank.count);signature=sig;started=tick;
        }
        if(!bank.count) return {};
        unsigned elapsed=unsigned(std::max<int64_t>(0,tick-started));
        unsigned rate=arpRateTicks(settings), phase=elapsed%rate;
        unsigned gate=std::max(1u,rate*settings.gate/100);
        if(phase>=gate) return {};
        PitchSet out;out.count=1;out.notes[0]=bank.notes[arpIndex(settings.arp,elapsed/rate,bank.count)];return out;
    }
    bool onset(const ToolsSettings& settings,int64_t tick) const {
        return bank.count && (tick-started)%arpRateTicks(settings)==0;
    }
private:
    PitchSet source,bank;int64_t started=0;unsigned signature=255;
};
}
