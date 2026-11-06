#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "OscillatorTables.h"
namespace pocket {
// Range-reduced sixth-order exp2 approximation, below 0.02 cent error.
inline float modulatedPitchRatio(float semitones){
    float x=std::max(-8.f,std::min(8.f,semitones/12.f));int octave=int(x);if(x<octave)--octave;float f=x-octave;
    float value=1.f+f*(.69314718056f+f*(.24022650696f+f*(.05550410866f+f*(.00961812911f+f*(.00133335581f+f*.00015403530f)))));
    return octave>=0?value*float(1u<<octave):value/float(1u<<(-octave));
}
__attribute__((always_inline)) inline float oscillatorSineRaw(float phase){
    static const int16_t table[129]={0,555,1108,1660,2207,2749,3284,3811,4329,4837,5333,5816,6285,6739,7177,7598,8000,8383,8745,9087,9407,9704,9978,10227,10452,10652,10826,10974,11096,11191,11259,11300,11314,11300,11259,11191,11096,10974,10826,10652,10452,10227,9978,9704,9407,9087,8745,8383,8000,7598,7177,6739,6285,5816,5333,4837,4329,3811,3284,2749,2207,1660,1108,555,0,-555,-1108,-1660,-2207,-2749,-3284,-3811,-4329,-4837,-5333,-5816,-6285,-6739,-7177,-7598,-8000,-8383,-8745,-9087,-9407,-9704,-9978,-10227,-10452,-10652,-10826,-10974,-11096,-11191,-11259,-11300,-11314,-11300,-11259,-11191,-11096,-10974,-10826,-10652,-10452,-10227,-9978,-9704,-9407,-9087,-8745,-8383,-8000,-7598,-7177,-6739,-6285,-5816,-5333,-4837,-4329,-3811,-3284,-2749,-2207,-1660,-1108,-555,0};
    float at=phase*128.f;unsigned i=std::min(127u,unsigned(at));return table[i]+(table[i+1]-table[i])*(at-i);
}
__attribute__((always_inline)) inline float oscillatorSine(float phase){return oscillatorSineRaw(phase)*(1.f/11314.f);}
__attribute__((always_inline)) inline float oscillatorBlep(float phase,float dt){
    if(dt<=0)return 0;
    if(phase<dt){float t=phase/dt;return t+t-t*t-1;}
    if(phase>1-dt){float t=(phase-1)/dt;return t*t+t+t+1;}
    return 0;
}
__attribute__((always_inline)) inline float oscillatorValue(unsigned shape,float phase,float increment,uint32_t& noise){
    if(shape==5){noise^=noise<<13;noise^=noise>>17;noise^=noise<<5;return (int(noise&65535)-32768)*.42286f;}
    if(increment>=.49f)return 0; // Digital oscillator frequencies stay below Nyquist.
    if(shape==1)return oscillatorSineRaw(phase);
    if(shape==2)return (1-4*std::fabs(phase-.5f))*13856.f;
    if(shape==3||shape==4)return oscillatorMip(shape,phase,increment);
    return 0;
}
__attribute__((always_inline)) inline void advanceOscillator(float& phase,float increment){phase+=increment;if(phase>=1)phase-=unsigned(phase);}
__attribute__((always_inline)) inline float sampleLfo(unsigned shape,float phase){
    if(shape==1)return 1-4*std::fabs(phase-.5f);
    if(shape==2)return phase<.5f?1.f:-1.f;
    if(shape==3)return 2*phase-1;
    return oscillatorSine(phase);
}
inline const char* oscillatorName(unsigned shape){static const char* names[]={"SOURCE","SINE","TRI","SQUARE","SAW","NOISE"};return names[std::min(shape,5u)];}
inline const char* lfoTargetName(unsigned target){static const char* names[]={"OFF","PITCH","FILTER","AMP"};return names[std::min(target,3u)];}
inline const char* lfoShapeName(unsigned shape){static const char* names[]={"SINE","TRI","SQUARE","SAW"};return names[std::min(shape,3u)];}
}
