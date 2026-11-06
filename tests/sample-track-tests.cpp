#include "Workstation.h"
#include "SamplerEngine.h"
#include "SampleTrigger.h"
#include "SavedProject.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace pocket;
void check(bool ok,const char* msg){if(!ok){std::fprintf(stderr,"FAIL: %s\n",msg);std::exit(1);}}
std::vector<int16_t> pcm(24000);
struct Adapter {
 SamplerEngine tracks[3];bool attached[3]{};
 SampleSettings sound;
 Adapter(){sound.root=60;sound.end=pcm.size();sound.attack=0;sound.decay=0;sound.sustain=100;sound.release=10;sound.mode=SampleMode::Loop;}
 static void apply(void* p,unsigned track,unsigned slot,const Controls& c){
  auto& a=*static_cast<Adapter*>(p);a.tracks[track].setPolyphony(4);
  if(slot>=4){a.tracks[track].attach(nullptr,0,24000);a.attached[track]=false;return;}
  if(!a.attached[track]){a.tracks[track].attach(pcm.data(),pcm.size(),24000);a.attached[track]=true;}
  SampleControl v;v.sound=a.sound;v.master=c.volume;v.panic=c.panic;a.tracks[track].applyMidi(v,c.midiNotes,c.retriggerNotes);
 }
 static void render(void* p,unsigned track,int16_t* out,unsigned n){static_cast<Adapter*>(p)->tracks[track].render(out,n);}
};
std::vector<int16_t> render(Workstation& engine,Input& in,unsigned n){std::vector<int16_t> result(n);for(unsigned at=0;at<n;){unsigned k=std::min(128u,n-at);engine.apply(in);engine.render(result.data()+at,k);at+=k;}return result;}
double rms(const std::vector<int16_t>& values){double sum=0;for(auto v:values){check(std::abs(int(v))<=32112,"mixed PCM remains bounded");sum+=double(v)*v;}return std::sqrt(sum/values.size());}
int main(){
 for(unsigned i=0;i<pcm.size();++i)pcm[i]=int16_t(std::sin(2*Pi*440*i/24000)*8000);
 Adapter adapter;Workstation engine;engine.init();engine.onSampleTrack(&adapter,Adapter::apply,Adapter::render);
 Input in;in.song.drums.volume=100;in.song.drumMuted=true;in.song.tracks[1].muted=in.song.tracks[2].muted=true;in.song.tracks[0].wave=4;in.liveKeys=1;
 auto live=render(engine,in,4800);check(rms(live)>1000,"S1 sample source renders through workstation master");
 adapter.sound.loopRegion=true;adapter.sound.loopStart=3000;adapter.sound.loopEnd=10000;adapter.sound.loopFade=128;
 render(engine,in,30000);check(adapter.tracks[0].active()==1&&adapter.tracks[0].position()>=3000&&adapter.tracks[0].position()<=10000,"synth source uses custom sampler sustain region");
 in.liveKeys=0;render(engine,in,2000);check(!adapter.tracks[0].active(),"synth source releases custom sampler loop");in.liveKeys=1;adapter.sound.loopRegion=false;
 in.song.tracks[0].muted=true;render(engine,in,6000);check(rms(render(engine,in,2400))<1,"synth track mute settles to silence through gain smoothing");
 in.song.tracks[0].muted=false;in.song.tracks[0].tools.chord=true;
 render(engine,in,2400);check(adapter.tracks[0].active()==3,"diatonic chord reaches sample source as three MIDI notes");
 in.song.tracks[0].tools.chord=false;in.liveKeys=(1u<<0)|(1u<<4)|(1u<<7)|(1u<<11);
 render(engine,in,2400);check(adapter.tracks[0].active()==4,"complete seventh chord keeps all four sample voices");
 in.liveKeys=0;render(engine,in,2400);check(adapter.tracks[0].active()==0,"all four seventh chord voices release");
 in.liveKeys=0;in.song.tracks[0].tools.chord=false;in.playing=true;in.epoch++;
 auto& track=in.song.tracks[0];track.count=2;track.events[0]={0,24,60,100};track.events[1]={24,24,72,100};
 auto loop=render(engine,in,12000);check(rms(loop)>400,"saved melody plays through sampler source");
 in.song.tracks[0].wave=0;render(engine,in,256);check(!adapter.attached[0],"return to oscillator drops sampler voices");
 auto bytes=encodeSavedProject(in.song,"sample song",1);SavedProject restored;check(decodeSavedProject(bytes.data(),bytes.size(),restored),"PSYN06 project roundtrip");
 in.song.tracks[0].wave=7;bytes=encodeSavedProject(in.song,"sample song",2);check(decodeSavedProject(bytes.data(),bytes.size(),restored)&&restored.song.tracks[0].wave==7,"four sample slot IDs persist in unchanged slot size");
 auto legacy=encodeProject(Project{});legacy[5]='5';legacy[8]=5;uint32_t crc=crc32(legacy.data(),legacy.size()-4);for(unsigned b=0;b<4;++b)legacy[legacy.size()-4+b]=crc>>(8*b);
 Project old;check(decodeProject(legacy.data(),legacy.size(),old)&&old.tracks[0].wave==0,"PSYN05 oscillator projects remain readable");
 SamplerEngine direct;direct.attach(pcm.data(),pcm.size(),24000);SampleControl control;control.sound=adapter.sound;
 std::array<uint32_t,4> notes{},trigger{};notes[60/32]=1u<<(60%32);direct.applyMidi(control,notes);int16_t scratch[256];direct.render(scratch,256);
 trigger=notes;direct.applyMidi(control,notes,trigger);direct.render(scratch,256);check(direct.active()==1,"retriggered loop crossfades old voice instead of accumulating held voices");
 notes.fill(0);notes[0]=1u<<24;notes[2]=1u<<0;notes[2]|=1u<<31;notes[3]=1u<<23;direct.applyMidi(control,notes);direct.render(scratch,256);check(direct.active()<=4,"widely spaced MIDI notes respect four voice limit");
 int16_t ring[3072],quiet[512]{};SampleTrigger detector;detector.reset(ring,3072,1);
 for(unsigned i=0;i<80;++i)check(!detector.observe(quiet,512,false),"warmup cannot trigger");
 for(unsigned i=0;i<40;++i)check(!detector.observe(quiet,512,true),"quiet monitor does not trigger");
 int16_t knock[512]{};knock[0]=12000;knock[1]=-8000;check(detector.observe(knock,512,true),"two-sample knock at chunk start triggers");
 unsigned count=detector.commit();check(count==3072&&detector.onset==2560&&ring[2560]==12000&&ring[2561]==-8000,"pre-roll keeps exact onset and preceding silence in order");
 detector.reset(ring,3072,1);for(unsigned i=0;i<80;++i){for(unsigned j=0;j<512;++j)quiet[j]=(j%2)?80:-80;detector.observe(quiet,512,false);}
 check(!detector.observe(quiet,512,true),"steady background tracks RMS threshold");knock[0]=10000;check(detector.observe(knock,512,true),"knock still triggers above steady noise");
 std::puts("PASS: sample source live/chord/loop/mute/master, PSYN06 and legacy project storage, MIDI range/retrigger bounds, transient onset/pre-roll/noise triggering");
}
