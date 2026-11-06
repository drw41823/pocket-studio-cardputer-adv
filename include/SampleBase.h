#pragma once
#include "SamplerEngine.h"
namespace pocket {
enum class SampleBase : unsigned { Sine,Triangle,Square,Saw,Noise,Count };
constexpr unsigned SampleBaseFrames=16384,SampleBaseRate=33488,SampleBasePeriod=128;
inline const char* sampleBaseName(unsigned kind){static const char* names[]={"SINE C4","TRIANGLE C4","SQUARE C4","SAW C4","WHITE NOISE"};return names[std::min(kind,4u)];}
// Integer-period C4 (33488/128 Hz) loops without drift. Equal RMS source levels;
// AUTO LEVEL stays off so peak normalization does not undo that balance.
inline bool generateSampleBase(int16_t* out,unsigned frames,SampleBase kind,void (*service)()=nullptr){
    if(!out||!frames||unsigned(kind)>=unsigned(SampleBase::Count))return false;
    if(kind==SampleBase::Noise){
        uint32_t state=0x914b3a7du;
        for(unsigned i=0;i<frames;++i){state^=state<<13;state^=state>>17;state^=state<<5;out[i]=int16_t((int(state&65535)-32768)*.42286f);if(service&&!(i%128))service();}
        return true;
    }
    float cycle[SampleBasePeriod],mean=0,energy=0;
    for(unsigned i=0;i<SampleBasePeriod;++i){float phase=float(i)/SampleBasePeriod,value=0;
        if(kind==SampleBase::Sine)value=std::sin(2*Pi*phase);
        else if(kind==SampleBase::Triangle)value=1-4*std::fabs(phase-.5f);
        else if(kind==SampleBase::Square)value=i<SampleBasePeriod/2?1.f:-1.f;
        else value=2*phase-1;
        cycle[i]=value;mean+=value;
    }
    mean/=SampleBasePeriod;for(float& value:cycle){value-=mean;energy+=value*value;}
    float level=8000.f/std::sqrt(energy/SampleBasePeriod);
    for(unsigned i=0;i<frames;++i){out[i]=int16_t(cycle[i%SampleBasePeriod]*level);if(service&&!(i%128))service();}
    return true;
}
// Migrate older base presets only when the entire PCM exactly matches a
// generator. A periodic recording or a matching title alone is insufficient.
inline unsigned identifySampleBase(const int16_t* data,unsigned frames,unsigned rate){
    if(!data||frames!=SampleBaseFrames||rate!=SampleBaseRate)return 0;
    int16_t cycle[SampleBasePeriod];
    for(unsigned kind=0;kind<5;++kind){
        generateSampleBase(cycle,SampleBasePeriod,SampleBase(kind));
        bool match=true;for(unsigned i=0;i<SampleBasePeriod;++i)if(data[i]!=cycle[i]){match=false;break;}
        if(!match)continue;
        if(kind<4){for(unsigned i=SampleBasePeriod;i<frames;++i)if(data[i]!=cycle[i%SampleBasePeriod]){match=false;break;}}
        else {uint32_t state=0x914b3a7du;for(unsigned i=0;i<frames;++i){state^=state<<13;state^=state>>17;state^=state<<5;if(data[i]!=int16_t((int(state&65535)-32768)*.42286f)){match=false;break;}}}
        if(match)return kind+1;
    }
    return 0;
}
inline SampleSettings sampleBaseSettings(){SampleSettings s;s.end=SampleBaseFrames;s.root=60;s.mode=SampleMode::Loop;s.loopFade=0;s.attack=5;s.decay=0;s.sustain=100;s.release=100;s.autoLevel=false;return s;}
}
