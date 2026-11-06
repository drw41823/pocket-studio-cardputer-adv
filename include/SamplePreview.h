#pragma once
#include "SamplerEngine.h"
namespace pocket {
inline float previewEnvelope(float milliseconds,const SampleSettings& s,float hold) {
    float sustain=s.sustain/100.f;
    auto held=[&](float t){if(s.attack&&t<s.attack)return t/s.attack;t-=s.attack;if(s.decay&&t<s.decay)return 1.f-(1.f-sustain)*t/s.decay;return sustain;};
    if(milliseconds<hold)return held(milliseconds);
    return held(hold)*std::max(0.f,1.f-(milliseconds-hold)/std::max(1u,unsigned(s.release)));
}
// Render exactly one selected source pass; the ENV diagram has its own time axis.
struct SampleTonePreview : SampleWaveform {
    unsigned holdFrames=0, durationFrames=0;
    SampleWaveform cycle;bool hasCycle=false;
    float envelopeAt(float fraction,const SampleSettings& sound) const {
        float hold=std::max(800.f,float(sound.attack)+sound.decay+100.f);
        bool released=sound.mode!=SampleMode::OneShot;
        float total=hold+(released?sound.release:0);
        return previewEnvelope(fraction*total,sound,released?hold:total+1);
    }
    float envelopeHoldFraction(const SampleSettings& sound) const {
        float hold=std::max(800.f,float(sound.attack)+sound.decay+100.f);
        return hold/(hold+(sound.mode==SampleMode::OneShot?0:sound.release));
    }
    __attribute__((noinline)) void build(const int16_t* data,unsigned frames,unsigned rate,const SampleSettings& sound,void (*service)()=nullptr) {
        holdFrames=durationFrames=0;peak=0;hasCycle=false;cycle=SampleWaveform{};std::fill(low,low+Bins,0);std::fill(high,high+Bins,0);
        if(!data||frames<32||rate<8000||sound.start>=sound.end||sound.end>frames)return;
        SamplerEngine preview;preview.attach(data,frames,rate);SampleControl c;c.sound=sound;c.sound.mode=SampleMode::Gate;c.sound.loopRegion=false;c.base=sound.root;c.keys=1;c.master=200;preview.apply(c);
        // No simulated repeats or key-up: one crop, including its natural ending.
        unsigned outputFrames=unsigned((uint64_t(sound.end-sound.start)*SampleRate+rate-1)/rate);
        holdFrames=durationFrames=outputFrames;
        int16_t block[128];unsigned at=0;
        for(unsigned x=0;x<Bins;++x){
            unsigned end=std::max(1u,unsigned((uint64_t(x+1)*outputFrames+Bins-1)/Bins));int16_t lo=x?low[x-1]:0,hi=x?high[x-1]:0;
            if(end>at){lo=32767;hi=-32768;}
            while(at<end){unsigned n=std::min(128u,end-at);preview.render(block,n);if(service)service();
                for(unsigned i=0;i<n;++i){lo=std::min(lo,block[i]);hi=std::max(hi,block[i]);}at+=n;
            }
            low[x]=lo;high[x]=hi;peak=std::max(peak,std::max(unsigned(std::abs(int(lo))),unsigned(std::abs(int(hi)))));
        }
        // A stable, processed cycle reference for exact periodic source material.
        // The long-time pitch/filter sweeps are drawn separately as time guides.
        if(sound.oscA==5||(sound.oscB==5&&sound.oscMix))return;
        if(!sound.oscA){if(sound.end-sound.start<512)return;
            for(unsigned i=0;i<384;++i)if(std::abs(int(data[sound.start+i])-int(data[sound.start+i+128]))>2)return;}
        c.sound=sound;c.sound.mode=SampleMode::Loop;c.sound.loopRegion=false;c.sound.loopFade=0;
        c.sound.attack=0;c.sound.decay=0;c.sound.sustain=100;c.sound.pitchEnv=0;c.sound.filterEnv=0;c.sound.lfoTarget=0;
        preview.attach(data,frames,rate);preview.apply(c);
        for(unsigned i=0;i<16;++i){preview.render(block,128);if(service)service();}
        unsigned period=std::max(2u,unsigned(std::round(128.f*SampleRate/rate/std::pow(2.f,(sound.transpose+sound.fine/100.f)/12.f))));
        at=0;
        for(unsigned x=0;x<Bins;++x){unsigned end=std::max(1u,unsigned((uint64_t(x+1)*period+Bins-1)/Bins));
            int16_t lo=x?cycle.low[x-1]:0,hi=x?cycle.high[x-1]:0;if(end>at){lo=32767;hi=-32768;}
            while(at<end){unsigned n=std::min(128u,end-at);preview.render(block,n);if(service)service();for(unsigned i=0;i<n;++i){lo=std::min(lo,block[i]);hi=std::max(hi,block[i]);}at+=n;}
            cycle.low[x]=lo;cycle.high[x]=hi;cycle.peak=std::max(cycle.peak,std::max(unsigned(std::abs(int(lo))),unsigned(std::abs(int(hi)))));
        }
        hasCycle=true;
    }
};
}
