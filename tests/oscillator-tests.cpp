#include "SampleBase.h"
#include "SamplePreview.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cmath>
using namespace pocket;
void check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
std::vector<int16_t> source(SampleBaseFrames);
void skip(SamplerEngine& e,unsigned frames){int16_t out[128];while(frames){unsigned n=std::min(frames,128u);e.render(out,n);frames-=n;}}
double energy(SamplerEngine& e,unsigned frames){int16_t out[128];double sum=0;while(frames){unsigned n=std::min(frames,128u);e.render(out,n);for(unsigned i=0;i<n;++i){check(std::abs(int(out[i]))<=32112,"bounded output");sum+=double(out[i])*out[i];}frames-=n;}return sum;}
unsigned crossings(SamplerEngine& e,unsigned frames){std::vector<int16_t> out(frames);e.render(out.data(),frames);unsigned n=0;for(unsigned i=1;i<frames;++i)n+=out[i-1]<=0&&out[i]>0;return n;}
void start(SamplerEngine& e,const SampleControl& c){e.attach(source.data(),source.size(),SampleBaseRate);e.apply(c);}
int main(){
 for(int cents=-9600;cents<=9600;cents+=7){float semitones=cents/100.f;float expected=std::pow(2.f,semitones/12.f);float error=std::fabs(1200.f*std::log2(modulatedPitchRatio(semitones)/expected));check(error<.02f,"fast modulation pitch within 0.02 cent");}
 generateSampleBase(source.data(),source.size(),SampleBase::Square);
 SampleControl c;c.sound=sampleBaseSettings();c.sound.attack=0;c.base=60;c.keys=1;c.master=100;
 for(unsigned shape=1;shape<=5;++shape){uint32_t noise=1;double sum=0;for(unsigned i=0;i<1024;++i){float value=oscillatorValue(shape,i/1024.f,1.f/1024,noise);check(std::isfinite(value)&&std::fabs(value)<18000,"oscillator values finite and bounded");sum+=value*value;}double rms=std::sqrt(sum/1024);check(rms>6500&&rms<9000,"oscillator sources have comparable RMS");}
 uint32_t noise=1;check(oscillatorValue(4,.3f,.5f,noise)==0,"above-Nyquist tonal oscillator is suppressed");
 SampleControl dry=c;SamplerEngine original,disabled;start(original,dry);dry.sound.oscMix=100;dry.sound.oscTune=-24;dry.sound.lfoDepth=100;dry.sound.lfoRate=200;dry.sound.lfoShape=3;start(disabled,dry);std::vector<int16_t>a(12000),b(12000);original.render(a.data(),a.size());disabled.render(b.data(),b.size());check(a==b,"disabled oscillators/LFO preserve exact old output regardless of unused parameters");
 c.sound.oscB=1;c.sound.oscMix=100;SamplerEngine base;start(base,c);skip(base,4096);unsigned fundamental=crossings(base,4800);check(fundamental>=51&&fundamental<=54,"B oscillator follows base C4");
 c.sound.oscTune=12;SamplerEngine octave;start(octave,c);skip(octave,4096);unsigned doubled=crossings(octave,4800);check(doubled>=103&&doubled<=106,"B octave interval doubles frequency");
 c.sound.oscTune=0;SamplerEngine onlyA,onlyB,half;c.sound.oscMix=0;start(onlyA,c);c.sound.oscMix=100;start(onlyB,c);c.sound.oscMix=50;start(half,c);skip(onlyA,8192);skip(onlyB,8192);skip(half,8192);std::vector<int16_t> ca(2048),cb(2048),cm(2048);onlyA.render(ca.data(),ca.size());onlyB.render(cb.data(),cb.size());half.render(cm.data(),cm.size());for(unsigned i=0;i<cm.size();++i)check(std::abs(int(cm[i])-(int(ca[i])+cb[i])/2)<=4,"mixed output is normalized weighted A/B blend");
 c.sound.oscB=0;c.sound.oscA=1;c.sound.lfoTarget=3;c.sound.lfoRate=10;c.sound.lfoDepth=100;c.sound.lfoShape=0;SamplerEngine tremolo;start(tremolo,c);skip(tremolo,4800);double low=energy(tremolo,2400);skip(tremolo,9600);double high=energy(tremolo,2400);check(high>low*100,"AMP LFO makes periodic tremolo");
 SamplerEngine longHold;start(longHold,c);skip(longHold,12*SampleRate+4800);low=energy(longHold,2400);skip(longHold,9600);high=energy(longHold,2400);check(high>low*100&&longHold.active()==1,"LFO continues beyond ten seconds while holding");
 c.sound.oscA=4;c.sound.lfoTarget=2;c.sound.cutoff=50;SamplerEngine filter;start(filter,c);skip(filter,4800);high=energy(filter,2400);skip(filter,9600);low=energy(filter,2400);check(high>low*10,"FILTER LFO repeatedly opens and closes the filter");
 c.sound.oscA=1;c.sound.lfoTarget=1;c.sound.lfoShape=2;c.sound.cutoff=100;SamplerEngine vibrato;start(vibrato,c);skip(vibrato,4800);unsigned up=crossings(vibrato,4800);skip(vibrato,7200);unsigned down=crossings(vibrato,4800);check(up>down*1.15,"PITCH LFO changes oscillator frequency in both directions");
 c.sound.oscA=4;c.sound.oscB=3;c.sound.oscMix=65;c.sound.oscTune=7;c.sound.lfoTarget=2;c.sound.lfoRate=200;c.sound.lfoShape=3;c.sound.lfoDepth=100;c.sound.cutoff=40;c.sound.resonance=90;c.sound.drive=100;c.sound.bits=4;c.sound.pitchEnv=24;c.sound.pitchDecay=2000;c.keys=15;c.master=200;SamplerEngine stress;start(stress,c);check(energy(stress,4*SampleRate)>0&&stress.active()==4,"four mixed/modulated voices remain audible and bounded");c.keys=0;stress.apply(c);skip(stress,SampleRate);check(!stress.active(),"mixed/modulated voices release without hanging");
 c.keys=3;SamplerEngine whole,split;start(whole,c);start(split,c);a.resize(16000);b.resize(16000);whole.render(a.data(),a.size());for(unsigned at=0;at<b.size();){unsigned n=std::min(7u,unsigned(b.size()-at));split.apply(c);split.render(b.data()+at,n);at+=n;}check(a==b,"dual oscillator and LFO timing independent of render/apply block sizes");
 SamplerEngine limited;c.keys=15;limited.setPolyphony(3);start(limited,c);check(limited.active()==3,"Studio sample source honors three-note polyphony");limited.setPolyphony(2);check(limited.active()==2,"lowering voice limit releases extra voices");
 SampleTonePreview view;c.sound=sampleBaseSettings();c.sound.oscA=1;c.sound.oscB=4;c.sound.oscMix=40;view.build(source.data(),source.size(),SampleBaseRate,c.sound);check(view.hasCycle&&view.cycle.peak>1000,"oscillator blend is rendered by the actual preview engine");c.sound.oscA=5;view.build(source.data(),source.size(),SampleBaseRate,c.sound);check(!view.hasCycle,"noise never claims a fixed cycle");
 std::puts("PASS: dual oscillators, interval/mix, RMS/headroom, LFO pitch/filter/amp, long hold, release, block timing and preview");
}
