#pragma once
#include "MusicState.h"
#include "SampleRegion.h"
#include "SampleEffects.h"
#include "BasicOscillator.h"
#include <cmath>
#include <algorithm>

namespace pocket {
enum class SampleMode : uint8_t { Gate, OneShot, Loop };
struct SampleSettings {
    uint32_t start=0, end=0;
    uint32_t loopStart=0,loopEnd=0;
    uint16_t loopFade=64;bool loopRegion=false;
    uint16_t attack=5, decay=100, release=100;
    uint8_t sustain=80, gain=80, root=60;
    SampleMode mode=SampleMode::Gate;
    bool autoLevel=true;
    uint8_t cutoff=100,resonance=0,drive=0,bits=16;
    int8_t pitchEnv=0,filterEnv=0,transpose=0,fine=0;
    uint16_t pitchDecay=150,filterDecay=300;
    uint8_t oscA=0,oscB=0,oscMix=50;int8_t oscTune=0;
    uint8_t lfoTarget=0,lfoShape=0,lfoDepth=25;uint16_t lfoRate=20; // tenths of Hz
};
// Keep custom sustain points inside the current crop after moving trim boundaries.
inline void fitSampleLoop(SampleSettings& s) {
    if(!s.loopRegion)return;
    s.loopStart=std::max(s.start,std::min(s.loopStart,s.end));
    s.loopEnd=std::max(s.start,std::min(s.loopEnd,s.end));
    if(s.loopEnd<=s.loopStart||s.loopEnd-s.loopStart<2){s.loopRegion=false;s.loopStart=s.loopEnd=0;}
}
inline unsigned sampleLoopStart(const SampleSettings& s){return s.loopRegion?s.loopStart:s.start;}
inline unsigned sampleLoopEnd(const SampleSettings& s){return s.loopRegion?s.loopEnd:s.end;}
inline unsigned sampleLoopFade(const SampleSettings& s){return std::min(unsigned(s.loopFade),(sampleLoopEnd(s)-sampleLoopStart(s))/4);}
// Decaying offset: initial signed amount returns to zero in the selected time.
inline float sampleSweep(unsigned elapsed,unsigned milliseconds) {
    return milliseconds?std::max(0.f,1.f-float(elapsed)/(milliseconds*24.f)):0.f;
}
struct SampleControl {
    SampleSettings sound;
    uint32_t keys=0, panic=0;
    unsigned base=60, master=100;
};
class SamplerEngine {
public:
    void setPolyphony(unsigned limit){voiceLimit=std::max(1u,std::min(4u,limit));for(unsigned i=voiceLimit;i<4;++i)voices[i]=Voice{};}
    void attach(const int16_t* samples,unsigned count,unsigned rate) { stop();gain=0;data=samples;frames=count;sourceRate=rate;levelStart=levelEnd=0;regionPeak=0;normalizer=1;effects=SampleEffects{};oscBlend=0; }
    void stop() { for(auto& v:voices) v=Voice{}; previous.fill(0);effects.resetState(); }
    void apply(const SampleControl& value) {
        std::array<uint32_t,4> notes{};
        for(unsigned key=0;key<24;++key)if((value.keys&(1u<<key))&&value.base<128&&key<128-value.base)notes[(value.base+key)/32]|=1u<<((value.base+key)%32);
        applyMidi(value,notes);
    }
#if defined(ARDUINO_ARCH_ESP32)
    IRAM_ATTR __attribute__((optimize("O3")))
#endif
    void applyMidi(const SampleControl& value,const std::array<uint32_t,4>& notes,const std::array<uint32_t,4>& retrigger={}) {
        if(value.panic!=control.panic) stop();
        if(value.sound.oscTune!=control.sound.oscTune)bRatio=std::pow(2.f,value.sound.oscTune/12.f);
        bool pitchChanged=value.sound.root!=control.sound.root||value.sound.transpose!=control.sound.transpose||value.sound.fine!=control.sound.fine;control=value;
        effects.configure((control.sound.filterEnv||(control.sound.lfoTarget==2&&control.sound.lfoDepth))?100:control.sound.cutoff,control.sound.resonance,control.sound.drive,control.sound.bits);
        if(!data || frames<32 || !sourceRate) {stop();return;}
        control.sound.start=std::min<unsigned>(control.sound.start,frames-2);
        control.sound.end=std::max<unsigned>(control.sound.start+2,std::min<unsigned>(frames,control.sound.end));
        fitSampleLoop(control.sound);
        control.sound.loopFade=std::min(2400u,unsigned(control.sound.loopFade));
        // Virtual oscillators have known peak bounds and need no PCM scan.
        // Source-only legacy patches retain their exact region normalization.
        if(control.sound.autoLevel){
            if(!control.sound.oscA&&(levelStart!=control.sound.start||levelEnd!=control.sound.end)){
                levelStart=control.sound.start;levelEnd=control.sound.end;regionPeak=samplePeak(data+levelStart,levelEnd-levelStart);}
            static const unsigned peaks[6]={0,11314,13856,10000,18000,13856};
            unsigned a=control.sound.oscA?peaks[std::min(5u,unsigned(control.sound.oscA))]:regionPeak;
            float blend=control.sound.oscB?std::min(100u,unsigned(control.sound.oscMix))/100.f:0;
            normalizer=sampleLevelGain(unsigned(a*(1-blend)+peaks[std::min(5u,unsigned(control.sound.oscB))]*blend));
        }
        if(pitchChanged)for(auto& v:voices)if(v.key>=0)v.increment=float(sourceRate)/SampleRate*std::pow(2.f,(v.key-int(control.sound.root)+control.sound.transpose+control.sound.fine/100.f)/12.f);
        for(auto& v:voices) if(v.key>=0 && !(notes[unsigned(v.key)/32]&(1u<<(unsigned(v.key)%32))) && control.sound.mode!=SampleMode::OneShot && v.stage!=3) {
            v.stage=3;v.releaseDelta=v.level/std::max(1u,unsigned(control.sound.release)*SampleRate/1000);
        }
        for(unsigned word=0;word<4;++word){uint32_t onsets=(notes[word]&~previous[word])|retrigger[word];while(onsets){unsigned midi=word*32+unsigned(__builtin_ctz(onsets));onsets&=onsets-1;
            if(!(notes[midi/32]&(1u<<(midi%32))))continue;
            if(retrigger[midi/32]&(1u<<(midi%32)))for(auto& v:voices)if(v.key==int(midi)&&v.stage!=3){v.stage=3;v.releaseDelta=v.level/48.f;}
            Voice* next=&voices[0];
            for(unsigned i=0;i<voiceLimit;++i){auto& v=voices[i];if(v.key<0){next=&v;break;}if(v.age<next->age)next=&v;}
            *next=Voice{};next->key=int(midi);next->age=++age;next->position=float(control.sound.start);next->noiseA=0x9e3779b9u^age;next->noiseB=0x812f359du^age;
            next->increment=float(sourceRate)/SampleRate*std::pow(2.f,(int(midi)-int(control.sound.root)+control.sound.transpose+control.sound.fine/100.f)/12.f);
        }
        }
        previous=notes;
    }
    unsigned position() const { for(const auto& v:voices) if(v.key>=0) return unsigned(v.position); return control.sound.start; }
    unsigned active() const {unsigned n=0;for(const auto& v:voices) n+=v.key>=0;return n;}
#if defined(ARDUINO_ARCH_ESP32)
    IRAM_ATTR __attribute__((optimize("O3")))
#endif
    void render(int16_t* out,unsigned count) {
        const auto& settings=control.sound;
        const unsigned loopStart=sampleLoopStart(settings),loopEnd=sampleLoopEnd(settings);
        const unsigned loopFade=loopEnd>loopStart?sampleLoopFade(settings):0;
        const bool filtered=settings.filterEnv||(settings.lfoTarget==2&&settings.lfoDepth);
        const float lfoStep=std::min(200u,unsigned(settings.lfoRate))/(10.f*SampleRate);
        const float mixTarget=settings.oscB?std::min(100u,unsigned(settings.oscMix))/100.f:0;
        const float sustain=settings.sustain/100.f;
        const float attackStep=1.f/std::max(48u,unsigned(settings.attack)*SampleRate/1000);
        const float decayStep=(1-sustain)/std::max(1u,unsigned(settings.decay)*SampleRate/1000);
        const float targetGain=std::min(100u,unsigned(settings.gain))/100.f*std::min(200u,control.master)/100.f*.5f*(settings.autoLevel?normalizer:1.f);
        for(unsigned i=0;i<count;++i) {
            float sum=0;bool sounding=false;oscBlend+=(mixTarget-oscBlend)*.002f;
            const auto& s=control.sound;
            for(auto& v:voices) {
                if(v.key<0 || !data || s.end<=s.start) continue;
                if(v.position>=(s.mode==SampleMode::Loop?loopEnd:s.end)) {
                    if(s.mode==SampleMode::Loop) {
                        v.position=loopStart+loopFade+std::fmod(v.position-loopEnd,float(loopEnd-loopStart-loopFade));
                    } else {v=Voice{};continue;}
                }
                if(v.stage==0) {
                    v.level+=attackStep;
                    if(v.level>=1) {v.level=1;v.stage=1;}
                } else if(v.stage==1) {
                    v.level-=decayStep;
                    if(v.level<=sustain) {v.level=sustain;v.stage=2;}
                } else if(v.stage==2) {
                    v.level+=(sustain-v.level)*.01f;
                } else if(v.stage==3) {
                    v.level-=v.releaseDelta;
                    if(v.level<=0) {v=Voice{};continue;}
                }
                // Modulation is voice-local and independent of render/apply block sizes.
                if(!v.modClock){
                    float lfo=s.lfoTarget&&s.lfoDepth?sampleLfo(s.lfoShape,v.lfoPhase):0;
                    float semitones=s.pitchEnv*sampleSweep(v.elapsed,s.pitchDecay)+(s.lfoTarget==1?lfo*s.lfoDepth*.02f:0);
                    v.pitchRatio=semitones?(s.lfoTarget&&s.lfoDepth?modulatedPitchRatio(semitones):std::pow(2.f,semitones/12.f)):1.f;
                    v.lfoAmplitude=s.lfoTarget==3?1.f-(lfo*.5f+.5f)*s.lfoDepth/100.f:1.f;
                    if(filtered){int cut=int(std::round(s.cutoff+s.filterEnv*sampleSweep(v.elapsed,s.filterDecay)+(s.lfoTarget==2?lfo*s.lfoDepth*.5f:0)));
                        v.filter.configure(std::max(0,std::min(100,cut)),s.resonance,0,16,s.lfoTarget&&s.lfoDepth);}
                }
                v.modClock=(v.modClock+1)&31u;
                if(s.lfoTarget)advanceOscillator(v.lfoPhase,lfoStep);
                float value=s.oscA?0:sample(v.position,s.mode==SampleMode::Loop?loopEnd:s.end);
                if(!s.oscA&&s.mode==SampleMode::Loop) {
                    if(loopFade && v.position>=loopEnd-loopFade) {
                        float fraction=(v.position-(loopEnd-loopFade))/loopFade;
                        value=value*(1-fraction)+sample(loopStart+(v.position-(loopEnd-loopFade)),loopEnd)*fraction;
                    }
                } else if(!s.oscA) {
                    // A short end fade avoids a discontinuity even in one-shot mode.
                    float remaining=(s.end-v.position)/std::max(.001f,v.increment);
                    value*=std::min(1.f,remaining/48.f);
                }
                if(s.oscA||s.oscB){
                    float step=v.increment*v.pitchRatio/128.f;
                    if(s.oscA)value=oscillatorValue(s.oscA,v.phaseA,step,v.noiseA);
                    if(s.oscB||oscBlend>.0001f){float second=s.oscB?oscillatorValue(s.oscB,v.phaseB,step*bRatio,v.noiseB):0;value=value*(1-oscBlend)+second*oscBlend;}
                    advanceOscillator(v.phaseA,step);advanceOscillator(v.phaseB,step*bRatio);
                }
                if(filtered)value=v.filter.process(value);
                sounding=true;sum+=value*v.level*v.lfoAmplitude;v.position+=v.increment*v.pitchRatio;
                if(v.elapsed<240000u)++v.elapsed;
            }
            gain+=(targetGain-gain)*.002f;if(!sounding)effects.resetState();float value=effects.process(sum*gain);
            if(std::fabs(value)>24000.f) {float excess=std::fabs(value)-24000;value=std::copysign(24000+excess/(1+excess/8112),value);}
            out[i]=int16_t(std::max(-32112.f,std::min(32112.f,value)));
        }
    }
private:
    struct Voice {int key=-1;uint32_t age=0;float position=0,increment=1,level=0,releaseDelta=0;unsigned stage=0,elapsed=0,modClock=0;float pitchRatio=1,phaseA=0,phaseB=0,lfoPhase=0,lfoAmplitude=1;uint32_t noiseA=1,noiseB=2;SampleEffects filter;};
    Voice voices[4];SampleEffects effects;
    const int16_t* data=nullptr;
    unsigned voiceLimit=4,frames=0,sourceRate=0,levelStart=0,levelEnd=0,regionPeak=0;std::array<uint32_t,4> previous{};uint32_t age=0;float gain=0,normalizer=1,bRatio=1,oscBlend=0;
    SampleControl control;
    __attribute__((always_inline)) float sample(float pos,unsigned end) const {
        unsigned at=std::min<unsigned>(unsigned(pos),end-1), next=std::min<unsigned>(at+1,end-1);
        return data[at]+(data[next]-data[at])*(pos-at);
    }
};
}
