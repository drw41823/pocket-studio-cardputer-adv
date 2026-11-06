#pragma once
#include "SamplerEngine.h"
#include <cstring>

namespace pocket {
constexpr unsigned LegacySamplePatchBytes=160,LoopSamplePatchBytes=176,ModSamplePatchBytes=192,SamplePatchBytes=208;
struct SamplePatch {
    char title[16]{}, path[96]{};
    uint32_t frames=0, rate=0, fingerprint=0;
    SampleSettings sound;
    uint8_t baseSource=0; // 0=sample, 1..5=built-in source kind+1; PSMP05 reserved byte 180.
};
inline void patch32(uint8_t* p,uint32_t value) {for(unsigned i=0;i<4;++i)p[i]=value>>(i*8);}
inline uint32_t patch32(const uint8_t* p) {return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
inline bool validPatch(const SamplePatch& p) {
    return p.baseSource<=5 && p.title[15]==0 && p.path[95]==0 && p.path[0] && p.frames>=32 && p.frames<=32768 && p.rate>=8000 && p.rate<=48000
        && p.sound.start<p.sound.end && p.sound.end<=p.frames && p.sound.end-p.sound.start>=2
        && p.sound.root>=24 && p.sound.root<=96 && unsigned(p.sound.mode)<=2
        && p.sound.attack<=1000 && p.sound.decay<=2000 && p.sound.release>=5 && p.sound.release<=2000
        && p.sound.sustain<=100 && p.sound.gain<=100
        && (!p.sound.loopRegion||(p.sound.loopStart>=p.sound.start&&p.sound.loopEnd<=p.sound.end&&p.sound.loopEnd>p.sound.loopStart&&p.sound.loopEnd-p.sound.loopStart>=2))
        && p.sound.pitchEnv>=-48 && p.sound.pitchEnv<=48 && p.sound.filterEnv>=-100 && p.sound.filterEnv<=100
        && p.sound.transpose>=-24 && p.sound.transpose<=24 && p.sound.fine>=-100 && p.sound.fine<=100
        && p.sound.pitchDecay<=2000 && p.sound.filterDecay<=2000
        && p.sound.oscA<=5 && p.sound.oscB<=5 && p.sound.oscMix<=100 && p.sound.oscTune>=-24 && p.sound.oscTune<=24
        && p.sound.lfoTarget<=3 && p.sound.lfoShape<=3 && p.sound.lfoDepth<=100 && p.sound.lfoRate>=1 && p.sound.lfoRate<=200
        && p.sound.loopFade<=2400
        && p.sound.cutoff<=100 && p.sound.resonance<=90 && p.sound.drive<=100 && p.sound.bits>=4 && p.sound.bits<=16;
}
inline void encodeSamplePatch(const SamplePatch& p,uint8_t* b) {
    std::fill(b,b+SamplePatchBytes,0);std::memcpy(b,"PSMP06",6);
    std::memcpy(b+8,p.title,16);std::memcpy(b+24,p.path,96);
    patch32(b+120,p.frames);patch32(b+124,p.rate);patch32(b+128,p.fingerprint);
    patch32(b+132,p.sound.start);patch32(b+136,p.sound.end);b[140]=p.sound.root;b[141]=unsigned(p.sound.mode);
    b[142]=p.sound.attack;b[143]=p.sound.attack>>8;b[144]=p.sound.decay;b[145]=p.sound.decay>>8;
    b[146]=p.sound.sustain;b[148]=p.sound.release;b[149]=p.sound.release>>8;b[150]=p.sound.gain;
    b[151]=p.sound.autoLevel?1:0;
    b[147]=p.sound.cutoff;b[152]=p.sound.resonance;b[153]=p.sound.drive;b[154]=p.sound.bits;
    patch32(b+160,p.sound.loopStart);patch32(b+164,p.sound.loopEnd);
    b[168]=p.sound.loopFade;b[169]=p.sound.loopFade>>8;b[170]=p.sound.loopRegion?1:0;
    b[172]=uint8_t(p.sound.pitchEnv);b[173]=uint8_t(p.sound.filterEnv);
    b[174]=uint8_t(p.sound.transpose);b[175]=uint8_t(p.sound.fine);
    b[176]=p.sound.pitchDecay;b[177]=p.sound.pitchDecay>>8;b[178]=p.sound.filterDecay;b[179]=p.sound.filterDecay>>8;
    b[180]=p.baseSource;
    b[181]=p.sound.oscA;b[182]=p.sound.oscB;b[183]=p.sound.oscMix;b[184]=uint8_t(p.sound.oscTune);
    b[185]=p.sound.lfoTarget;b[186]=p.sound.lfoShape;b[187]=p.sound.lfoDepth;b[188]=p.sound.lfoRate;b[189]=p.sound.lfoRate>>8;
    patch32(b+204,crc32(b,204));
}
inline bool decodeSamplePatch(const uint8_t* b,unsigned size,SamplePatch& out) {
    bool current=size==SamplePatchBytes&&!std::memcmp(b,"PSMP06",6);
    bool previous=size==ModSamplePatchBytes&&!std::memcmp(b,"PSMP05",6);
    bool loop=size==LoopSamplePatchBytes&&!std::memcmp(b,"PSMP04",6);
    bool legacy=size==LegacySamplePatchBytes&&(!std::memcmp(b,"PSMP01",6)||!std::memcmp(b,"PSMP02",6)||!std::memcmp(b,"PSMP03",6));
    unsigned check=current?204:previous?188:loop?172:156;
    if((!current&&!previous&&!loop&&!legacy)||patch32(b+check)!=crc32(b,check))return false;
    bool modern=b[5]!='1';if(modern&&b[151]>1)return false;
    SamplePatch p;std::memcpy(p.title,b+8,16);std::memcpy(p.path,b+24,96);
    p.frames=patch32(b+120);p.rate=patch32(b+124);p.fingerprint=patch32(b+128);
    p.sound.start=patch32(b+132);p.sound.end=patch32(b+136);p.sound.root=b[140];p.sound.mode=SampleMode(b[141]);
    p.sound.attack=b[142]|unsigned(b[143])<<8;p.sound.decay=b[144]|unsigned(b[145])<<8;
    p.sound.sustain=b[146];p.sound.release=b[148]|unsigned(b[149])<<8;p.sound.gain=b[150];
    p.sound.autoLevel=modern?bool(b[151]):true;
    if(b[5]=='3'||loop||previous||current){p.sound.cutoff=b[147];p.sound.resonance=b[152];p.sound.drive=b[153];p.sound.bits=b[154];}
    if(current||previous||loop){if(b[170]>1)return false;p.sound.loopStart=patch32(b+160);p.sound.loopEnd=patch32(b+164);p.sound.loopFade=b[168]|unsigned(b[169])<<8;p.sound.loopRegion=b[170];}
    if(current||previous){p.baseSource=b[180];p.sound.pitchEnv=int8_t(b[172]);p.sound.filterEnv=int8_t(b[173]);p.sound.transpose=int8_t(b[174]);p.sound.fine=int8_t(b[175]);p.sound.pitchDecay=b[176]|unsigned(b[177])<<8;p.sound.filterDecay=b[178]|unsigned(b[179])<<8;}
    if(current){p.sound.oscA=b[181];p.sound.oscB=b[182];p.sound.oscMix=b[183];p.sound.oscTune=int8_t(b[184]);p.sound.lfoTarget=b[185];p.sound.lfoShape=b[186];p.sound.lfoDepth=b[187];p.sound.lfoRate=b[188]|unsigned(b[189])<<8;}
    if(!validPatch(p))return false;out=p;return true;
}
}
