#include "WavReader.h"
#include "SamplerPatch.h"
#include "SampleEdit.h"
#include "SampleMemory.h"
#include "SamplePreview.h"
#include "SampleBase.h"
#include "TakeTrim.h"
#include "TakePreviewAudio.h"
#include "ToneNavigation.h"
#include "SamplerNavigation.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
using namespace pocket;
void require(bool ok,const char* text){if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
bool readVector(void* context,uint32_t at,void* out,unsigned n){auto& b=*static_cast<std::vector<uint8_t>*>(context);if(uint64_t(at)+n>b.size())return false;std::memcpy(out,b.data()+at,n);return true;}
void word(std::vector<uint8_t>& b,unsigned n,unsigned count){for(unsigned i=0;i<count;++i)b.push_back(n>>(8*i));}
std::vector<uint8_t> wav(unsigned channels=1,unsigned frames=512,unsigned rate=24000){
 std::vector<uint8_t>b;auto tag=[&](const char* t){b.insert(b.end(),t,t+4);};
 tag("RIFF");word(b,0,4);tag("WAVE");tag("JUNK");word(b,3,4);b.insert(b.end(),{1,2,3,0});
 tag("fmt ");word(b,16,4);word(b,1,2);word(b,channels,2);word(b,rate,4);word(b,rate*channels*2,4);word(b,channels*2,2);word(b,16,2);
 tag("data");word(b,frames*channels*2,4);
 for(unsigned i=0;i<frames;++i)for(unsigned ch=0;ch<channels;++ch)word(b,uint16_t(ch?-10000:10000),2);
 patch32(b.data()+4,b.size()-8);return b;
}
std::vector<int16_t> tone(unsigned rate,unsigned frames){std::vector<int16_t>b(frames);for(unsigned i=0;i<frames;++i)b[i]=int16_t(std::sin(2*Pi*440*i/rate)*12000);return b;}
void render(SamplerEngine& engine,unsigned frames){int16_t out[128];while(frames){unsigned n=std::min(frames,128u);engine.render(out,n);frames-=n;}}
double measure(SamplerEngine& engine,unsigned frames){int16_t out[128];double sum=0;while(frames){unsigned n=std::min(frames,128u);engine.render(out,n);for(unsigned i=0;i<n;++i){require(std::abs(int(out[i]))<=32112,"PCM within bound");sum+=double(out[i])*out[i];}frames-=n;}return sum;}
unsigned crossings(SamplerEngine& engine){std::vector<int16_t>out(2400);engine.render(out.data(),out.size());unsigned n=0;for(unsigned i=1;i<out.size();++i)n+=out[i-1]<=0&&out[i]>0;return n;}
int main(){
 ToneNavigation navigation;bool seen[30]{};
 require(navigation.field()==0&&navigation.groups,"Tone opens at group selection without editing first parameter");
 require(navigation.move(false,false,false,true,false)==0&&navigation.group==1&&navigation.groups,"left/right at entry select groups without editing values");
 navigation.move(false,false,true,false,false);navigation.move(false,true,false,false,false);
 require(!navigation.groups&&navigation.field()==0,"Down enters the selected group without Enter");
 for(unsigned g: {0u,1u,2u,3u,4u,5u,7u}){
  for(unsigned row=0;row<ToneNavigation::count(g);++row){unsigned field=navigation.field();require(field<30&&!seen[field],"each parameter reachable exactly once in grouped navigation");seen[field]=true;require(navigation.move(false,false,false,true,false)==1&&navigation.field()==field,"right edits selected parameter without Enter or changing selection");if(row+1<ToneNavigation::count(g))navigation.move(false,true,false,false,false);}
  navigation.move(false,false,false,false,true);require(navigation.groups,"Enter returns to group row");
  require(navigation.move(false,false,false,true,false)==0,"group navigation never edits sound parameters");
  require(navigation.group==(g==5?7:g==7?0:g+1),"right chooses next functional group");navigation.move(false,true,false,false,false);
 }
 navigation.select(5);require(navigation.group==1&&navigation.row==3,"release is grouped with envelope");navigation.move(false,true,false,false,false);require(navigation.row==3,"last row does not wrap into unrelated controls");
 navigation.select(8);navigation.move(true,false,false,false,false);require(navigation.groups&&navigation.group==2,"up from first row focuses its group");navigation.move(false,false,true,false,false);require(navigation.group==1,"left selects previous group");navigation.move(false,false,false,false,true);require(!navigation.groups&&navigation.field()==5,"Enter restores the previously selected parameter in this group");require(navigation.back()&&!navigation.back(),"Back returns to groups then lets caller leave editor");
 navigation.select(14);navigation.focusGroups();navigation.move(false,false,true,false,false);navigation.move(false,true,false,false,false);require(navigation.field()==11,"FX group remembers its last visited parameter");navigation.focusGroups();navigation.move(false,false,false,true,false);navigation.move(false,true,false,false,false);require(navigation.field()==14,"Loop group restores its own remembered row");
 navigation.move(true,false,false,false,false);navigation.move(true,false,false,false,false);navigation.move(true,false,false,false,false);require(navigation.groups,"Up through the first parameter returns to group selection");
 navigation.enterParameters();require(!navigation.groups&&navigation.row==0,"return from first row resumes first row");


 require(!canStageSample(92276,65536),"64KB persistent duplicate keeps 32KB reserve");
 require(canStageSample(92276,65536,65536),"replacing old PCM fits transient 16KB and final 32KB reserves");
 require(!canStageSample(80000,65536,65536),"replacement cannot borrow below transient floor");
 ToneNavigation baseNav;baseNav.setBasic(true);require(baseNav.rows()==2&&baseNav.field()==6,"basic sound group exposes gain/auto, not sample root/mode");
 unsigned baseGroups[7]={0,1,2,3,5,6,7};bool baseSeen[30]{};
 for(unsigned g:baseGroups){require(baseNav.group==g&&baseNav.groups,"basic group order skips loop");baseNav.enterParameters();for(unsigned row=0;row<baseNav.rows();++row){unsigned field=baseNav.field();require(field!=0&&field!=1&&(field<12||field>15)&&!baseSeen[field],"basic shaping controls are unique and omit sample mechanics");baseSeen[field]=true;baseNav.move(false,true,false,false,false);}baseNav.focusGroups();baseNav.move(false,false,false,true,false);}
 require(baseNav.group==0,"basic group navigation wraps correctly");baseNav.move(false,false,true,false,false);require(baseNav.group==7,"reverse basic navigation skips loop");baseNav.setBasic(false);baseNav.select(12);require(baseNav.group==4&&baseNav.field()==12,"returning to sample mode restores loop controls");
 require(samplerNextPage(0,false,true)==2&&samplerNextPage(2,true,true)==0&&samplerNextPage(3,false,true)==0&&samplerNextPage(0,true,true)==3,"basic tabs skip trim in both directions");require(samplerNextPage(0,false,false)==1&&samplerNextPage(2,true,false)==1,"recorded/imported samples retain trim navigation");
 require(samplerFileCount(true)==5&&samplerFileCount(false)==7,"basic files only expose useful actions");for(unsigned i=0;i<samplerFileCount(true);++i){unsigned action=samplerFileActionAt(i,true);require(action!=4&&action!=7&&action!=8,"basic files omit trim, raw export and destructive processing");}
 require(!canStageSample(92276,65536,1000),"replacement cannot end below persistent reserve");
 require(!canStageSample(32768,0xffffffffu,65536),"allocation budget arithmetic cannot wrap");


 // Long takes are accepted only by the SD recording browser. Instrument loads
 // remain bounded. Verify extracted PCM matches the selected source exactly.
 auto longTake=wav(2,96000);WavInfo longInfo,window;
 require(inspectWav(readVector,&longTake,longTake.size(),longInfo)==WavError::TooLong,"long take cannot bypass instrument RAM limit");
 require(inspectWav(readVector,&longTake,longTake.size(),longInfo,0xffffffffu)==WavError::None,"SD take parser accepts four seconds stereo");
 require(!wavWindow(longInfo,0,32769,window)&&!wavWindow(longInfo,96000,96032,window)&&!wavWindow(longInfo,50,20,window)&&!wavWindow(longInfo,0,31,window),"invalid extraction range rejected");
 require(wavWindow(longInfo,95000,96000,window)&&window.frames==1000&&window.offset==longInfo.offset+95000*4,"end-of-file extraction offset respects stereo stride");
 std::vector<int16_t>clip(1002,1234);
 require(decodeWav(readVector,&longTake,window,clip.data()+1,1000),"long take window decoded within capacity");
 require(clip.front()==1234&&clip.back()==1234,"extraction preserves surrounding memory");
 for(unsigned i=1;i<=1000;++i)require(clip[i]==0,"selected stereo samples downmix exactly");
 WavInfo overflow=longInfo;overflow.offset=0xfffffff0u;
 require(!wavWindow(overflow,100,132,window),"extraction offset cannot overflow");

 SampleSettings envelope;envelope.attack=100;envelope.decay=100;envelope.sustain=50;envelope.release=100;
 require(previewEnvelope(0,envelope,400)==0&&std::abs(previewEnvelope(50,envelope,400)-.5f)<.001&&std::abs(previewEnvelope(200,envelope,400)-.5f)<.001,"preview reflects attack and sustain");
 require(std::abs(previewEnvelope(450,envelope,400)-.25f)<.001&&previewEnvelope(500,envelope,400)==0,"preview reflects release");
 auto quiet=tone(24000,12000);for(auto& v:quiet)v/=20;
 SampleControl level;level.keys=1;level.sound.root=60;level.base=60;level.sound.end=quiet.size();level.sound.mode=SampleMode::Loop;level.sound.attack=0;level.sound.decay=0;level.sound.sustain=100;
 SamplerEngine matched,raw;matched.attach(quiet.data(),quiet.size(),24000);raw.attach(quiet.data(),quiet.size(),24000);
 matched.apply(level);level.sound.autoLevel=false;raw.apply(level);render(matched,6000);render(raw,6000);
 require(measure(matched,2000)>measure(raw,2000)*100,"automatic level match raises quiet samples digitally");
 // A late, quiet selection must sound identical to an already-cut file,
 // even with loud excluded audio elsewhere and an initially wider selection.
 auto padded=std::vector<int16_t>(24000,0);auto selected=tone(24000,6000);
 for(auto& v:selected)v/=20;
 std::copy(selected.begin(),selected.end(),padded.begin()+12000);
 std::fill(padded.begin()+21000,padded.end(),32767);
 SampleWaveform cropWave;cropWave.build(padded.data(),padded.size(),12000,18000);
 require(cropWave.peak==samplePeak(selected.data(),selected.size()),"preview peak excludes silence and loud outside crop");
 SampleWaveform cutWave;cutWave.build(selected.data(),selected.size(),0,selected.size());
 require(std::equal(cropWave.low,cropWave.low+116,cutWave.low)&&std::equal(cropWave.high,cropWave.high+116,cutWave.high),"preview zooms selected region to entire width");
 cropWave.build(padded.data(),padded.size(),0,32);require(cropWave.peak==0&&cropWave.high[115]==0,"tiny silent crop replaces previous waveform without reading outside");
 SampleControl cropControl;cropControl.keys=1;cropControl.base=69;cropControl.sound.root=69;cropControl.sound.attack=100;cropControl.sound.start=12000;cropControl.sound.end=18000;
 SamplerEngine cropped,cut;cropped.attach(padded.data(),padded.size(),24000);cut.attach(selected.data(),selected.size(),24000);
 auto wide=cropControl;wide.keys=0;wide.sound.start=0;wide.sound.end=padded.size();cropped.apply(wide);
 cropped.apply(cropControl);cropControl.sound.start=0;cropControl.sound.end=selected.size();cut.apply(cropControl);
 std::vector<int16_t> croppedOut(4096),cutOut(4096);cropped.render(croppedOut.data(),croppedOut.size());cut.render(cutOut.data(),cutOut.size());
 require(croppedOut==cutOut,"ADSR starts at crop and level updates to selected peak after trim");
 require(samplePeak(croppedOut.data(),128)>0&&samplePeak(croppedOut.data(),4096)>1000,"late quiet crop sounds immediately with compensated level");
 // Colour controls run in the same engine as sampler and synth tracks.
 auto fxTone=tone(24000,12000);SampleControl fc;fc.sound.end=fxTone.size();fc.sound.mode=SampleMode::Loop;fc.sound.root=69;fc.base=69;fc.keys=1;fc.sound.attack=0;fc.sound.decay=0;fc.sound.sustain=100;fc.sound.autoLevel=false;
 auto fxEnergy=[&](SampleControl control){SamplerEngine e;e.attach(fxTone.data(),fxTone.size(),24000);e.apply(control);render(e,6000);return measure(e,2400);};
 double dryEnergy=fxEnergy(fc);fc.sound.cutoff=25;double darkEnergy=fxEnergy(fc);require(darkEnergy<dryEnergy*.08,"low cutoff audibly suppresses 440 Hz");
 fc.sound.cutoff=46;fc.sound.resonance=0;double flatEnergy=fxEnergy(fc);fc.sound.resonance=80;require(fxEnergy(fc)>flatEnergy*1.5,"resonance emphasizes sound near cutoff");
 fc.sound.cutoff=100;fc.sound.resonance=0;fc.sound.drive=80;require(fxEnergy(fc)>dryEnergy*1.5,"drive changes source waveshape and energy");
 fc.sound.drive=0;fc.sound.bits=4;SamplerEngine crushed;crushed.attach(fxTone.data(),fxTone.size(),24000);crushed.apply(fc);render(crushed,12000);int16_t crunch[1024];crushed.render(crunch,1024);unsigned stepped=0;for(auto v:crunch)stepped+=std::abs(int(v)-int(std::round(v/4096.f))*4096)<=2;require(stepped>1000,"bit reduction produces coarse amplitude steps");
 // 8-bit is real quantization, but its error is much quieter than 4-bit.
 SampleEffects eightBits;eightBits.configure(100,0,0,8);SampleEffects fourBits;fourBits.configure(100,0,0,4);double error8=0,error4=0;unsigned changed8=0;
 for(unsigned i=0;i<24000;++i){float v=10000*std::sin(2*Pi*437*i/24000.f);float q8=eightBits.process(v),q4=fourBits.process(v);require(std::abs(q8/256-std::round(q8/256))<.001f,"8-bit output uses 256-unit quantization steps");error8+=(q8-v)*(q8-v);error4+=(q4-v)*(q4-v);changed8+=std::abs(q8-v)>1;}
 require(changed8>20000&&error4>error8*100,"8-bit changes PCM while 4-bit has much larger audible quantization error");
 SampleTonePreview visible;fc.sound.bits=16;visible.build(fxTone.data(),fxTone.size(),24000,fc.sound);auto dryView=visible;fc.sound.cutoff=25;visible.build(fxTone.data(),fxTone.size(),24000,fc.sound);require(visible.peak<dryView.peak/3,"preview renders actual filter DSP rather than unprocessed source bins");
 // Display each selected source once, independent of held-note/loop duration.
 SampleSettings note=fc.sound;note.cutoff=100;note.resonance=0;note.drive=0;note.bits=16;note.mode=SampleMode::Loop;note.attack=60;note.decay=100;note.sustain=70;note.release=123;
 visible.build(fxTone.data(),fxTone.size(),24000,note);
 require(visible.durationFrames==fxTone.size(),"loop overview shows source once without simulated repeats");
 require(visible.envelopeAt(1,note)<.001f&&visible.peak>0,"separate ENV diagram includes release");
 auto singlePass=visible;note.release=300;visible.build(fxTone.data(),fxTone.size(),24000,note);
 require(visible.durationFrames==singlePass.durationFrames&&std::equal(visible.low,visible.low+116,singlePass.low)&&std::equal(visible.high,visible.high+116,singlePass.high),"release changes diagram without repeating or shortening source waveform");
 note.start=103;note.end=1103;visible.build(fxTone.data(),fxTone.size(),24000,note);require(visible.durationFrames==1000,"only selected crop appears once");
 visible.build(fxTone.data(),fxTone.size(),8000,note);require(visible.durationFrames==3000,"one source pass respects source sample rate");
 note.mode=SampleMode::Gate;visible.build(fxTone.data(),fxTone.size(),24000,note);require(visible.durationFrames==1000,"gate preview covers full crop once");
 note.mode=SampleMode::OneShot;require(visible.envelopeHoldFraction(note)==1,"one-shot diagram has no key-up stage");
 fc.sound.cutoff=0;fc.sound.resonance=90;fc.sound.drive=100;fc.sound.bits=4;fc.keys=0xffffff;fc.master=200;SamplerEngine stress;stress.attach(fxTone.data(),fxTone.size(),24000);stress.apply(fc);measure(stress,24000*3);
 for(unsigned cutoff:{0u,99u,100u,30u,90u,100u}){fc.sound.cutoff=cutoff;stress.apply(fc);measure(stress,2400);}
 fc.keys=0;++fc.panic;stress.apply(fc);require(measure(stress,512)==0,"effects cannot self-oscillate after panic");
 auto pitched=tone(24000,12000);SamplerEngine live;live.attach(pitched.data(),pitched.size(),24000);level.sound.autoLevel=false;level.sound.root=69;level.base=69;live.apply(level);render(live,6000);unsigned before=crossings(live);
 level.sound.root=81;live.apply(level);unsigned after=crossings(live);std::printf("Live pitch before=%u after=%u voices=%u\n",before,after,live.active());require(before>38&&before<50&&after>18&&after<26&&live.active()==1,"root changes held-note pitch without retrigger");
 double full=measure(live,2000);level.sound.sustain=20;live.apply(level);render(live,2000);require(measure(live,2000)<full*.1,"sustain changes audible level while key remains held");
 // Exported takes use an independently decoded standard WAV header and exact PCM.
 std::vector<int16_t> take={-32768,-2000,0,1000,32767};take.resize(512,123);
 std::vector<uint8_t> exported(44+take.size()*2);wav16Header(exported.data(),take.size(),24000);
 for(unsigned i=0;i<take.size();++i){exported[44+i*2]=uint16_t(take[i]);exported[45+i*2]=uint16_t(take[i])>>8;}
 WavInfo exportedInfo;require(inspectWav(readVector,&exported,exported.size(),exportedInfo)==WavError::None,"export header accepted by RIFF reader");
 std::vector<int16_t> exportedPCM(take.size());require(decodeWav(readVector,&exported,exportedInfo,exportedPCM.data(),exportedPCM.size())&&exportedPCM==take,"export PCM bit exact including int16 limits");
 int16_t normalized[]={123,-32768,-1000,1000,32767,456};
 editSample(normalized+1,4,24000,SampleEdit::Normalize);
 require(normalized[0]==123&&normalized[5]==456&&samplePeak(normalized+1,4)==29490,"normalization bounds and selected-region guards");
 auto original=take;editSample(take.data(),take.size(),24000,SampleEdit::Reverse);editSample(take.data(),take.size(),24000,SampleEdit::Reverse);
 require(take==original,"reverse twice restores exact PCM");
 int16_t constant[32];std::fill(constant,constant+32,10000);editSample(constant,32,24000,SampleEdit::RemoveDC);
 require(samplePeak(constant,32)==0,"DC removal returns constant input to zero");
 editSample(constant,32,24000,SampleEdit::Normalize);require(samplePeak(constant,32)==0,"silent normalize finite and silent");
 std::fill(constant,constant+32,10000);editSample(constant,32,24000,SampleEdit::Fade);
 require(constant[0]==0&&constant[31]==0&&constant[15]>0,"short sample bounded nonoverlapping fades");
 editSample(nullptr,0,24000,SampleEdit::Fade);
 WavInfo info;auto mono=wav();require(inspectWav(readVector,&mono,mono.size(),info)==WavError::None,"PCM with odd padded JUNK accepted");
 require(info.rate==24000&&info.frames==512&&info.channels==1,"WAV metadata");
 int16_t decoded[514];std::fill(decoded,decoded+514,1234);
 require(decodeWav(readVector,&mono,info,decoded+1,512)&&decoded[0]==1234&&decoded[513]==1234&&decoded[1]==10000,"mono decode and buffer guards");
 require(!decodeWav(readVector,&mono,info,decoded,511),"insufficient capacity rejected");
 auto stereo=wav(2);require(inspectWav(readVector,&stereo,stereo.size(),info)==WavError::None,"stereo accepted");
 require(decodeWav(readVector,&stereo,info,decoded,512)&&decoded[0]==0,"stereo averaged without overflow");
 auto truncated=mono;truncated.pop_back();require(inspectWav(readVector,&truncated,truncated.size(),info)==WavError::Truncated,"truncated container rejected");
 auto wrong=mono;wrong[32]=3;require(inspectWav(readVector,&wrong,wrong.size(),info)==WavError::Format,"float WAV rejected");
 auto longFile=wav(1,32769);require(inspectWav(readVector,&longFile,longFile.size(),info)==WavError::TooLong,"over-budget WAV rejected");
 auto bad=mono;patch32(bad.data()+4,0xffffffff);require(inspectWav(readVector,&bad,bad.size(),info)==WavError::Truncated,"RIFF length cannot overflow");
 auto shortFile=wav(1,4);require(inspectWav(readVector,&shortFile,shortFile.size(),info)==WavError::Format,"too-short sample rejected");
 for(unsigned size=0;size<mono.size();++size){WavInfo unused;require(inspectWav(readVector,&mono,size,unused)!=WavError::None,"every truncated file rejected");}
 for(unsigned rate:{16000u,24000u,48000u}){
  auto data=tone(rate,rate/2);SamplerEngine engine;engine.attach(data.data(),data.size(),rate);
  SampleControl c;c.sound.end=data.size();c.sound.root=69;c.base=69;c.sound.attack=0;c.sound.decay=0;c.sound.sustain=100;c.keys=1;
  engine.apply(c);render(engine,1200);unsigned n=crossings(engine);require(n>=43&&n<=45,"source sample rate preserves original pitch");
  engine.stop();c.base=81;engine.apply(c);render(engine,1200);n=crossings(engine);require(n>=87&&n<=89,"octave maps to double frequency");
 }
 auto data=tone(24000,12000);SampleControl c;c.sound.end=data.size();c.sound.root=69;c.base=69;c.sound.attack=0;c.sound.decay=0;c.sound.sustain=100;c.sound.release=10;c.keys=1;
 SamplerEngine gate;gate.attach(data.data(),data.size(),24000);gate.apply(c);require(measure(gate,2400)>1e8,"gate starts immediately");c.keys=0;gate.apply(c);render(gate,300);require(gate.active()==0&&measure(gate,128)==0,"gate release reaches exact silence");
 SamplerEngine one;one.attach(data.data(),data.size(),24000);c.sound.mode=SampleMode::OneShot;c.keys=1;one.apply(c);render(one,1000);c.keys=0;one.apply(c);require(measure(one,1000)>1e8,"one-shot continues after release");render(one,13000);require(one.active()==0&&measure(one,128)==0,"one-shot ends exactly");
 SamplerEngine loop;loop.attach(data.data(),data.size(),24000);c.sound.mode=SampleMode::Loop;c.sound.start=200;c.sound.end=600;c.keys=1;loop.apply(c);
 for(unsigned block=0;block<12000;++block){int16_t out[130];out[0]=1234;out[129]=1234;loop.render(out+1,128);require(out[0]==1234&&out[129]==1234,"render guard bytes");require(loop.position()>=200&&loop.position()<=600,"loop position stays in selected window");}
 c.keys=0;loop.apply(c);render(loop,300);require(loop.active()==0,"loop releases instead of hanging");
 c.keys=0xffffff;c.sound.gain=100;c.master=200;c.sound.start=0;c.sound.end=data.size();loop.apply(c);require(loop.active()<=4,"voice limit enforced");measure(loop,5000);
 ++c.panic;c.keys=0;loop.apply(c);require(measure(loop,128)==0,"panic stops all voices");
 std::vector<int16_t> tiny(32,32767);SamplerEngine bounded;bounded.attach(tiny.data(),tiny.size(),48000);c.keys=0xffffff;c.base=96;c.sound.root=24;c.sound.start=0xffffffff;c.sound.end=0;bounded.apply(c);measure(bounded,24000);
 // Same engine and controls must produce identical samples across host/UI block sizes.
 SampleControl stable;stable.keys=1;stable.sound.root=69;stable.base=69;stable.sound.end=data.size();stable.sound.mode=SampleMode::Loop;
 SamplerEngine whole,split;whole.attach(data.data(),data.size(),24000);split.attach(data.data(),data.size(),24000);whole.apply(stable);split.apply(stable);
 std::vector<int16_t>a(4096),b(4096);whole.render(a.data(),a.size());
 for(unsigned at=0;at<b.size();){unsigned n=std::min<unsigned>(7,b.size()-at);split.apply(stable);split.render(b.data()+at,n);at+=n;}
 require(a==b,"sample playback independent of block size");
 SamplePatch patch;std::strcpy(patch.title,"my sound");std::strcpy(patch.path,"/PocketSynth/Samples/bell.wav");patch.frames=12000;patch.rate=24000;patch.sound.end=12000;patch.fingerprint=42;patch.sound.cutoff=43;patch.sound.resonance=65;patch.sound.drive=30;patch.sound.bits=7;
 uint8_t bytes[SamplePatchBytes];encodeSamplePatch(patch,bytes);SamplePatch restored;
 require(decodeSamplePatch(bytes,sizeof bytes,restored)&&restored.fingerprint==42&&std::strcmp(restored.title,"my sound")==0,"named preset roundtrip");

 require(restored.sound.autoLevel,"new preset stores auto level");
 require(restored.sound.cutoff==43&&restored.sound.resonance==65&&restored.sound.drive==30&&restored.sound.bits==7,"all colour parameters survive sound save/load");
 bytes[5]='2';patch32(bytes+156,crc32(bytes,156));require(decodeSamplePatch(bytes,LegacySamplePatchBytes,restored)&&restored.sound.cutoff==100&&restored.sound.resonance==0&&restored.sound.drive==0&&restored.sound.bits==16,"PSMP02 legacy sounds retain bypassed colour effects");
 encodeSamplePatch(patch,bytes);bytes[154]=3;patch32(bytes+204,crc32(bytes,204));require(!decodeSamplePatch(bytes,sizeof bytes,restored),"invalid bit depth rejected");encodeSamplePatch(patch,bytes);

 bytes[5]='1';bytes[151]=0;patch32(bytes+156,crc32(bytes,156));require(decodeSamplePatch(bytes,LegacySamplePatchBytes,restored)&&restored.sound.autoLevel,"legacy PSMP01 loads with level matching");
 encodeSamplePatch(patch,bytes);bytes[151]=2;patch32(bytes+204,crc32(bytes,204));require(!decodeSamplePatch(bytes,sizeof bytes,restored),"invalid level flag rejected");encodeSamplePatch(patch,bytes);
 bytes[140]^=1;require(!decodeSamplePatch(bytes,sizeof bytes,restored),"corrupt preset rejected");
 encodeSamplePatch(patch,bytes);bytes[119]=1;patch32(bytes+204,crc32(bytes,204));require(!decodeSamplePatch(bytes,sizeof bytes,restored),"unterminated path rejected");
 encodeSamplePatch(patch,bytes);patch32(bytes+136,12001);patch32(bytes+204,crc32(bytes,204));require(!decodeSamplePatch(bytes,sizeof bytes,restored),"trim outside source rejected");
 // Full backward compatibility includes old KEY LOOP crossfade defaults.
 encodeSamplePatch(patch,bytes);bytes[5]='3';patch32(bytes+156,crc32(bytes,156));
 require(decodeSamplePatch(bytes,160,restored)&&!restored.sound.loopRegion&&restored.sound.loopFade==64&&restored.sound.cutoff==43,"PSMP03 retains old full-crop loop and effects");
 patch.sound.loopRegion=true;patch.sound.loopStart=1500;patch.sound.loopEnd=7000;patch.sound.loopFade=256;encodeSamplePatch(patch,bytes);
 require(decodeSamplePatch(bytes,sizeof bytes,restored)&&restored.sound.loopRegion&&restored.sound.loopStart==1500&&restored.sound.loopEnd==7000&&restored.sound.loopFade==256,"PSMP04 sustain region and crossfade roundtrip");
 patch32(bytes+164,12001);patch32(bytes+204,crc32(bytes,204));require(!decodeSamplePatch(bytes,sizeof bytes,restored),"loop outside source rejected");
 encodeSamplePatch(patch,bytes);bytes[170]=2;patch32(bytes+204,crc32(bytes,204));require(!decodeSamplePatch(bytes,sizeof bytes,restored),"invalid loop flag rejected");
 encodeSamplePatch(patch,bytes);require(!decodeSamplePatch(bytes,160,restored),"truncated PSMP04 cannot masquerade as legacy");
 // New envelope storage and signed boundaries; all earlier formats default dry.
 encodeSamplePatch(patch,bytes);bytes[5]='4';patch32(bytes+172,crc32(bytes,172));
 require(decodeSamplePatch(bytes,176,restored)&&restored.sound.pitchEnv==0&&restored.sound.filterEnv==0&&restored.sound.transpose==0&&restored.sound.fine==0,"PSMP04 modulation defaults are bypassed");
 patch.baseSource=3;patch.sound.pitchEnv=-48;patch.sound.pitchDecay=2000;patch.sound.filterEnv=100;patch.sound.filterDecay=730;patch.sound.transpose=-24;patch.sound.fine=-100;encodeSamplePatch(patch,bytes);
 require(decodeSamplePatch(bytes,sizeof bytes,restored)&&restored.baseSource==3&&restored.sound.pitchEnv==-48&&restored.sound.pitchDecay==2000&&restored.sound.filterEnv==100&&restored.sound.filterDecay==730&&restored.sound.transpose==-24&&restored.sound.fine==-100,"all signed modulation and tuning parameters roundtrip");
 bytes[180]=6;patch32(bytes+204,crc32(bytes,204));require(!decodeSamplePatch(bytes,sizeof bytes,restored),"invalid base source marker rejected");encodeSamplePatch(patch,bytes);
 bytes[172]=49;patch32(bytes+204,crc32(bytes,204));require(!decodeSamplePatch(bytes,sizeof bytes,restored),"out-of-range pitch envelope rejected");
 require(sampleSweep(0,100)==1.f&&sampleSweep(1200,100)==.5f&&sampleSweep(2400,100)==0.f&&sampleSweep(0,0)==0.f,"sweep has deterministic duration and zero-time bypass");
 auto probe=tone(24000,12000);SampleControl sweep;sweep.sound.end=probe.size();sweep.sound.root=69;sweep.base=69;sweep.keys=1;sweep.sound.mode=SampleMode::Loop;sweep.sound.autoLevel=false;sweep.sound.attack=0;sweep.sound.decay=0;sweep.sound.sustain=100;
 SamplerEngine falling;falling.attach(probe.data(),probe.size(),24000);sweep.sound.pitchEnv=24;sweep.sound.pitchDecay=500;falling.apply(sweep);unsigned firstCross=crossings(falling);render(falling,12000);unsigned lastCross=crossings(falling);
 require(firstCross>lastCross*2&&lastCross>=43&&lastCross<=45,"positive pitch sweep falls to original pitch");
 SamplerEngine rising;rising.attach(probe.data(),probe.size(),24000);sweep.sound.pitchEnv=-24;rising.apply(sweep);firstCross=crossings(rising);render(rising,12000);lastCross=crossings(rising);require(firstCross*2<lastCross&&lastCross>=43&&lastCross<=45,"negative pitch sweep rises to original pitch");
 sweep.sound.pitchEnv=0;sweep.sound.transpose=12;sweep.sound.fine=0;SamplerEngine tuneUp;tuneUp.attach(probe.data(),probe.size(),24000);tuneUp.apply(sweep);require(crossings(tuneUp)>=87,"transpose raises the sound by one octave");
 sweep.sound.transpose=0;sweep.sound.fine=100;SamplerEngine cents;cents.attach(probe.data(),probe.size(),24000);cents.apply(sweep);require(crossings(cents)>=46,"100 cents raises pitch one semitone");
 auto bright=probe;for(unsigned i=0;i<bright.size();++i)bright[i]=int16_t(std::sin(2*Pi*4000*i/24000)*10000);
 sweep.sound.fine=0;sweep.sound.cutoff=10;sweep.sound.filterEnv=90;sweep.sound.filterDecay=500;
 SamplerEngine closing;closing.attach(bright.data(),bright.size(),24000);closing.apply(sweep);double initialEnergy=measure(closing,2400);render(closing,12000);double finalEnergy=measure(closing,2400);require(initialEnergy>finalEnergy*20,"positive filter envelope starts bright and closes");
 sweep.sound.cutoff=99;sweep.sound.filterEnv=-90;SamplerEngine opening;opening.attach(bright.data(),bright.size(),24000);opening.apply(sweep);initialEnergy=measure(opening,2400);render(opening,12000);finalEnergy=measure(opening,2400);require(finalEnergy>initialEnergy*5,"negative filter envelope opens from dark to bright");
 sweep.sound.pitchEnv=-48;sweep.sound.pitchDecay=2000;sweep.sound.filterEnv=100;sweep.sound.resonance=90;sweep.sound.drive=100;sweep.sound.bits=4;sweep.keys=15;sweep.master=200;
 SamplerEngine modStress;modStress.attach(bright.data(),bright.size(),24000);modStress.apply(sweep);measure(modStress,24000*4);require(modStress.active()==4,"four independently modulated voices remain bounded");sweep.keys=0;modStress.apply(sweep);render(modStress,24000);require(!modStress.active(),"modulated voices release without hanging");
 sweep.keys=3;sweep.sound.pitchEnv=24;sweep.sound.filterEnv=-60;SamplerEngine modWhole,modSplit;modWhole.attach(probe.data(),probe.size(),24000);modSplit.attach(probe.data(),probe.size(),24000);modWhole.apply(sweep);modSplit.apply(sweep);std::vector<int16_t>ma(9000),mb(9000);modWhole.render(ma.data(),ma.size());for(unsigned at=0;at<mb.size();){unsigned n=std::min(7u,unsigned(mb.size()-at));modSplit.apply(sweep);modSplit.render(mb.data()+at,n);at+=n;}require(ma==mb,"modulation timing is independent of rendering and apply block sizes");
 // PSMP05 migration preserves modulation/base identity and defaults the new engine off.
 encodeSamplePatch(patch,bytes);bytes[5]='5';patch32(bytes+188,crc32(bytes,188));
 require(decodeSamplePatch(bytes,192,restored)&&restored.baseSource==3&&restored.sound.pitchEnv==-48&&restored.sound.oscA==0&&restored.sound.oscB==0&&restored.sound.lfoTarget==0,"PSMP05 reads with oscillator/LFO disabled");
 patch.sound.oscA=4;patch.sound.oscB=2;patch.sound.oscMix=37;patch.sound.oscTune=-12;patch.sound.lfoTarget=2;patch.sound.lfoShape=3;patch.sound.lfoRate=173;patch.sound.lfoDepth=65;encodeSamplePatch(patch,bytes);
 require(decodeSamplePatch(bytes,sizeof bytes,restored)&&restored.sound.oscA==4&&restored.sound.oscB==2&&restored.sound.oscMix==37&&restored.sound.oscTune==-12&&restored.sound.lfoTarget==2&&restored.sound.lfoShape==3&&restored.sound.lfoRate==173&&restored.sound.lfoDepth==65,"PSMP06 dual oscillators and LFO roundtrip");
 bytes[185]=4;patch32(bytes+204,crc32(bytes,204));require(!decodeSamplePatch(bytes,sizeof bytes,restored),"invalid LFO target rejected");
 encodeSamplePatch(patch,bytes);bytes[188]=0;bytes[189]=0;patch32(bytes+204,crc32(bytes,204));require(!decodeSamplePatch(bytes,sizeof bytes,restored),"zero LFO rate rejected");
 // An unmistakable negative attack must never recur inside a positive sustain region.
 std::vector<int16_t> regions(6000,10000);std::fill(regions.begin(),regions.begin()+1000,-10000);std::fill(regions.begin()+4000,regions.end(),-20000);
 SampleControl sustain;sustain.keys=1;sustain.sound.end=6000;sustain.sound.mode=SampleMode::Loop;sustain.sound.loopRegion=true;sustain.sound.loopStart=1500;sustain.sound.loopEnd=4000;sustain.sound.loopFade=0;sustain.sound.autoLevel=false;sustain.sound.attack=0;sustain.sound.decay=0;sustain.sound.sustain=100;sustain.sound.release=200;
 SamplerEngine instrument;instrument.attach(regions.data(),regions.size(),24000);instrument.apply(sustain);int16_t intro[512];instrument.render(intro,512);require(intro[500]<0,"original attack plays before loop region");render(instrument,5000);
 for(unsigned i=0;i<200;++i){instrument.render(intro,512);for(auto v:intro)require(v>0,"sustain region never replays attack or out-of-loop tail");require(instrument.position()>=1500&&instrument.position()<=4000,"held position remains in sustain region");}
 sustain.keys=0;instrument.apply(sustain);render(instrument,5000);require(!instrument.active()&&measure(instrument,256)==0,"custom loop release stops and becomes silent");
 // A custom loop and variable crossfade must also be independent of render blocks.
 sustain.keys=1;sustain.sound.loopFade=128;whole.attach(regions.data(),6000,24000);split.attach(regions.data(),6000,24000);whole.apply(sustain);split.apply(sustain);a.resize(16000);b.resize(16000);whole.render(a.data(),a.size());
 for(unsigned at=0;at<b.size();){unsigned n=std::min<unsigned>(7,b.size()-at);split.apply(sustain);split.render(b.data()+at,n);at+=n;}require(a==b,"custom sustain crossfade is block-size independent");
 sustain.sound.loopStart=0xffffffff;sustain.sound.loopEnd=1;sustain.sound.loopFade=65535;instrument.apply(sustain);measure(instrument,24000);require(instrument.active()>0,"malformed live loop settings are safely fitted to crop");
 SampleSettings fitted;fitted.start=1000;fitted.end=5000;fitted.loopRegion=true;fitted.loopStart=500;fitted.loopEnd=4000;fitSampleLoop(fitted);require(fitted.loopStart==1000&&fitted.loopEnd==4000,"trim intersects surviving loop points");fitted.start=4500;fitSampleLoop(fitted);require(!fitted.loopRegion,"trim removes disjoint custom loop safely");
 std::vector<int16_t> basePCM(SampleBaseFrames);
 require(std::abs(float(SampleBaseRate)/SampleBasePeriod-261.625565f)<.001f,"built-in periodic sources are tuned to C4");
 for(unsigned kind=0;kind<5;++kind){require(generateSampleBase(basePCM.data(),basePCM.size(),SampleBase(kind)),"all five basic sources generate");double sum=0,energy=0;for(auto v:basePCM){sum+=v;energy+=double(v)*v;require(std::abs(int(v))<16000,"basic source has bounded headroom");}
  double rms=std::sqrt(energy/basePCM.size());require(rms>7700&&rms<8300&&std::abs(sum/basePCM.size())<250,"basic sources have comparable RMS and low DC offset");
  if(kind<4)for(unsigned i=128;i<basePCM.size();++i)require(basePCM[i]==basePCM[i%128],"periodic source loops exactly without phase drift");
  SampleTonePreview cycleView;auto cycleSettings=sampleBaseSettings();cycleView.build(basePCM.data(),basePCM.size(),SampleBaseRate,cycleSettings);
  require(identifySampleBase(basePCM.data(),basePCM.size(),SampleBaseRate)==kind+1,"all five legacy generated sources are identified by full PCM");
  basePCM[basePCM.size()-1]^=1;require(identifySampleBase(basePCM.data(),basePCM.size(),SampleBaseRate)==0,"a modified tail is never misidentified as a canonical built-in source");basePCM[basePCM.size()-1]^=1;
  require(cycleView.hasCycle==(kind<4),"periodic bases get one-cycle cache; white noise keeps clip overview");
  if(kind<4){require(cycleView.cycle.peak>1000,"cycle preview contains audible processed source");auto dryCycle=cycleView.cycle;cycleSettings.cutoff=10;cycleView.build(basePCM.data(),basePCM.size(),SampleBaseRate,cycleSettings);require(cycleView.hasCycle&&cycleView.cycle.peak<dryCycle.peak/2,"single-cycle preview responds to real filter DSP");cycleSettings.gain=0;cycleView.build(basePCM.data(),basePCM.size(),SampleBaseRate,cycleSettings);require(cycleView.cycle.peak==0,"zero gain produces a flat single-cycle preview");}
  SamplerEngine baseEngine;baseEngine.attach(basePCM.data(),basePCM.size(),SampleBaseRate);SampleControl basic;basic.keys=1;basic.sound=sampleBaseSettings();basic.base=60;basic.master=100;baseEngine.apply(basic);
  require(measure(baseEngine,24000)>1000000&&baseEngine.active()==1,"each base source remains audible through held loop");basic.keys=0;baseEngine.apply(basic);render(baseEngine,3000);require(!baseEngine.active(),"base source releases normally");
 }
 require(!generateSampleBase(nullptr,128,SampleBase::Sine)&&!generateSampleBase(basePCM.data(),0,SampleBase::Sine)&&!generateSampleBase(basePCM.data(),128,SampleBase::Count),"invalid basic source generation is rejected");
 require(takeTrimStep(24000*60,24000,0,false,false,false)>=6206,"long-take default step scales with waveform pixels");
 require(takeTrimStep(24000*60,24000,1800,false,false,false)>=124120,"long hold accelerates long-take navigation");
 require(takeTrimStep(24000*60,24000,1800,true,true,true)==1&&takeTrimStep(24000*60,24000,0,false,false,true)==24000,"Fn exact sample and Ctrl exact second override acceleration");
 unsigned trimStart=10000,trimEnd=20000;moveTakeTrim(100000,0,2000,trimStart,trimEnd);require(trimStart==12000&&trimEnd==20000,"START edits only start boundary");
 moveTakeTrim(100000,1,-1000000,trimStart,trimEnd);require(trimEnd==trimStart+32,"END cannot cross minimum clip length");
 moveTakeTrim(100000,2,1000000,trimStart,trimEnd);require(trimEnd==100000&&trimStart==99968,"MOVE slides whole clip to far end without changing width");
 moveTakeTrim(100000,2,-1000000,trimStart,trimEnd);require(trimStart==0&&trimEnd==32,"MOVE slides whole clip to zero");
 TakeWave overview;overview.begin(100000);for(unsigned i=0;i<100000;++i)overview.append(i==50000?20000:i==50001?-12000:0);
 unsigned hit=uint64_t(50000)*116/100000;require(overview.done==100000&&overview.high[hit]==20000&&overview.low[hit]==-12000&&overview.peak==20000,"streaming whole-take waveform preserves one-sample transient peaks");
 overview.begin(32);for(unsigned i=0;i<32;++i)overview.append(i-16);require(overview.done==32&&overview.peak==16,"short clips safely leave unused waveform bins empty");overview.append(30000);require(overview.done==32&&overview.peak==16,"waveform cache never reads or appends beyond declared take");
 // Preview consumes existing 48kHz stream at a 24kHz sampler output clock.
 for(unsigned hz:{8000u,24000u,48000u}){AudioStream preview;preview.reset(hz);preview.volume=65;std::vector<int16_t> samples(1000,10000);require(preview.push(samples.data(),samples.size()),"preview bounded prebuffer");preview.eof=true;preview.playing=true;
  std::vector<int16_t> result(unsigned((uint64_t(1000)*24000+hz-1)/hz)+1000);renderTakeStream(preview,result.data(),result.size());
  require(preview.position==1000&&!preview.playing&&preview.finished&&preview.underruns==0,"preview consumes selected frames once at all supported source rates");
  require(*std::max_element(result.begin(),result.end())>1000&&std::abs(int(result.back()))<2,"preview is audible and decays to silence at selection end");
 }
 std::puts("PASS: bounded WAV parsing/decoding, rate/pitch mapping, polyphony, gate/one-shot/loop, release/panic, buffer bounds, block timing and named preset CRC");
}
