// Sampler records and undo snapshots live on SD. The editor reuses one
// 32768-frame PCM buffer. Cache mutations require the audio pause ticket.
enum class SamplerPage : uint8_t { Play, Trim, Tone, Files, Browse, Presets, Rename, Actions, Result, Record, Process, TakeTrim, Start, SessionLoad, Bases };
SamplerPage samplerPage=SamplerPage::Play,samplerReturn=SamplerPage::Play,samplerResultReturn=SamplerPage::Presets;
unsigned samplerAction=0,samplerFileAction=0,samplerLastMidi=0;
uint32_t samplerRepeatAt[4]{};
char samplerResult[40]{};bool samplerResultOK=false;
Preferences samplerLegacy;bool samplerLegacyOK=false,samplerStoreOK=false,samplerSaving=false,samplerConfirm=false;
bool samplerUnsaved=true,samplerEditing=false,samplerUndoReady=false,samplerCheckMode=false;
char samplerSoundDirectory[48]="/PocketSynth/Sounds";
const char* samplerExportDirectory="/PocketSynth/Samples";
ToneNavigation toneNav;
unsigned samplerStartChoice=0,samplerLoadChoice=0,samplerBaseChoice=0;bool samplerBaseFromFiles=false;
SamplePatch samplerWorkInfo;bool samplerWorkAvailable=false;
unsigned homeSelection=0,samplerField=0,samplerFile=0,samplerFiles=0,samplerSlot=0;
int16_t* samplerData=nullptr;
int16_t samplerMin[116]{},samplerMax[116]{};unsigned samplerWaveScale=64;
SampleTonePreview samplerToneWave;
void rebuildSampleToneWave(){samplerToneWave.build(samplerData,samplePatch.frames,samplePatch.rate,samplePatch.sound,serviceKeyboardHardware);}
char samplerFileNames[24][64]{},samplerName[16]{};
SamplePatch samplerSlotInfo[4],synthSampleInfo[4],samplerUndoPatch;
bool samplerSlotValid[4]{},samplerSlotPresent[4]{};
const char* sampleModes[]={"KEY GATE","ONE SHOT","KEY LOOP"};
int samplerBoundSlot=-1;
void sampleResult(bool,const char*);
bool exportSampleWav();bool openRecordedTake(const char*);bool stopTakePreview();
void updateSynthSoundSlot(unsigned);
int16_t* reserveSample(unsigned bytes,bool replacing=false);
void sampleSilence(){sampleControl.keys=0;++sampleControl.panic;publish();dirty=true;}
void releaseSamplePCM(int16_t* data){if(!data||data==samplerData)return;for(const auto& s:synthSamples)if(s.data==data)return;std::free(data);}
void dropSynthSampleCache(){for(auto& s:synthSamples){auto* old=const_cast<int16_t*>(s.data);s.data=nullptr;++s.generation;releaseSamplePCM(old);}publish();}
bool ensureSamplerSD(bool allowMount=true){
    if(sdOK){File root=SD.open("/");bool ok=bool(root);root.close();if(ok){samplerStoreOK=true;return true;}SD.end();sdOK=false;}
    if(!allowMount){samplerStoreOK=false;return false;}
    sdOK=SD.begin(12,SPI,4000000)&&SD.cardType()!=CARD_NONE;samplerStoreOK=sdOK;
    if(!sdOK)tell("Insert SD to use sampler");return sdOK;
}
struct SoundSD {
    bool exists(const char* p){return SD.exists(p);}
    bool remove(const char* p){return SD.remove(p);}
    bool rename(const char* a,const char* b){return SD.rename(a,b);}
    bool write(const char* p,const uint8_t* bytes,unsigned size){if(SD.exists(p)&&!SD.remove(p))return false;File f=SD.open(p,FILE_WRITE);bool ok=f&&f.write(bytes,size)==size;f.flush();f.close();return ok;}
    bool read(const char* p,uint8_t* bytes,unsigned size){File f=SD.open(p,FILE_READ);bool ok=f&&f.size()==size&&f.read(bytes,size)==int(size);f.close();return ok;}
} soundSD;
void soundPaths(unsigned slot,char* final,char* temporary,char* backup){snprintf(final,64,"%s/SLOT%u.psmp",samplerSoundDirectory,slot+1);snprintf(temporary,64,"%s/SLOT%u.tmp",samplerSoundDirectory,slot+1);snprintf(backup,64,"%s/SLOT%u.bak",samplerSoundDirectory,slot+1);}
bool writeSampleSlot(unsigned slot,const SamplePatch& patch){
    if(slot>=4||!ensureSamplerSD())return false;SD.mkdir("/PocketSynth");SD.mkdir(samplerSoundDirectory);
    char final[64],temporary[64],backup[64];soundPaths(slot,final,temporary,backup);
    return writeSoundRecord(soundSD,final,temporary,backup,patch);
}
bool readSampleSlot(unsigned slot,SamplePatch& patch,bool allowMount=true){
    if(slot>=4||!ensureSamplerSD(allowMount))return false;
    char final[64],temporary[64],backup[64];soundPaths(slot,final,temporary,backup);
    if(readSoundRecord(soundSD,final,patch)||readSoundRecord(soundSD,backup,patch))return true;
    if(samplerCheckMode||SD.exists(final)||SD.exists(backup))return false;
    // One-way migration: legacy Flash is read-only, and never overwritten.
    uint8_t bytes[LegacySamplePatchBytes];char key[8];snprintf(key,sizeof key,"slot%u",slot+1);
    return samplerLegacyOK&&samplerLegacy.getBytesLength(key)==sizeof bytes&&samplerLegacy.getBytes(key,bytes,sizeof bytes)==sizeof bytes
        &&decodeSamplePatch(bytes,sizeof bytes,patch)&&writeSampleSlot(slot,patch);
}
void refreshSampleSlots(){for(unsigned i=0;i<4;++i){samplerSlotValid[i]=readSampleSlot(i,samplerSlotInfo[i]);char f[64],t[64],b[64];soundPaths(i,f,t,b);samplerSlotPresent[i]=samplerSlotValid[i]||SD.exists(f)||SD.exists(b);}}
bool sampleReadAt(void* ctx,uint32_t offset,void* out,unsigned n){auto& f=*static_cast<File*>(ctx);return f.seek(offset)&&f.read(static_cast<uint8_t*>(out),n)==int(n);}
void rebuildSampleWave(){
    // Reuse the selected-region cache as scratch for the whole-file overview.
    SampleWaveform full;full.build(samplerData,samplePatch.frames,0,samplePatch.frames);
    std::copy(full.low,full.low+116,samplerMin);
    std::copy(full.high,full.high+116,samplerMax);
    samplerWaveScale=std::max(64u,full.peak);rebuildSampleToneWave();
}
bool checkpointEditor(){return !samplerData||!samplePatch.frames||(std::strncmp(samplePatch.path,"ram:",4)&&std::strcmp(samplePatch.path,"builtin:pluck"))||exportSampleWav();}
bool prepareEditorWrite(){
    if(!ensureSamplerSD()||!checkpointEditor()||!pauseForFlash())return false;
    sampleControl.keys=0;++sampleControl.panic;sampler.stop();dropSynthSampleCache();
    if(!samplerData){samplerData=static_cast<int16_t*>(std::malloc(65536));if(!samplerData){resumeAudio();tell("Cannot allocate playback buffer");return false;}}
    return true;
}
bool restoreEditor(const SamplePatch& old){
    File f=SD.open(old.path,FILE_READ);WavInfo info;
    bool ok=f&&inspectWav(sampleReadAt,&f,uint32_t(f.size()),info)==WavError::None&&info.frames==old.frames&&info.rate==old.rate
        &&decodeWav(sampleReadAt,&f,info,samplerData,32768)&&crc32(reinterpret_cast<uint8_t*>(samplerData),old.frames*2)==old.fingerprint;f.close();
    if(ok){samplePatch=old;toneNav.setBasic(old.baseSource!=0);sampler.attach(samplerData,old.frames,old.rate);}
    else {sampler.attach(nullptr,0,24000);std::free(samplerData);samplerData=nullptr;samplePatch=SamplePatch{};}
    rebuildSampleWave();publish();return ok;
}
__attribute__((noinline)) bool loadSampleSource(const char* path,const SamplePatch* restored=nullptr){
    if(!ensureSamplerSD())return false;if(std::strlen(path)>=sizeof samplePatch.path){tell("Sample path too long");return false;}
    sampleSilence();bool demo=!std::strcmp(path,"builtin:pluck");WavInfo info;File file;
    if(demo){info.frames=12000;info.rate=24000;info.channels=1;}
    else {file=SD.open(path,FILE_READ);if(!file||uint64_t(file.size())>0xffffffffu){tell("Sample WAV not found on SD");return false;}
        auto error=inspectWav(sampleReadAt,&file,uint32_t(file.size()),info);if(error==WavError::TooLong&&!restored){file.close();return openRecordedTake(path);}if(error!=WavError::None){file.close();tell(wavError(error));return false;}}
    SamplePatch wanted;if(restored){wanted=*restored;if(!validPatch(wanted)||wanted.frames!=info.frames||wanted.rate!=info.rate){tell("Sample settings mismatch");return false;}}
    else {snprintf(wanted.path,sizeof wanted.path,"%s",path);snprintf(wanted.title,sizeof wanted.title,"%.15s",demo?"DEMO PLUCK":std::strrchr(path,'/')?std::strrchr(path,'/')+1:path);wanted.frames=info.frames;wanted.rate=info.rate;wanted.sound.end=info.frames;}
    if(!prepareEditorWrite()){file.close();return false;}SamplePatch previous=samplePatch;bool had=previous.frames>=32;
    bool ok=true;
    if(demo){for(unsigned i=0;i<info.frames;++i){float t=float(i)/info.rate,phase=2*Pi*261.625565f*t;float v=(std::sin(phase)+.4f*std::sin(phase*2.003f)+.2f*std::sin(phase*4.01f))*std::exp(-t*7.f);samplerData[i]=int16_t(v*10000.f*std::min(1.f,t*500.f));}}
    else ok=decodeWav(sampleReadAt,&file,info,samplerData,32768);file.close();
    auto hash=ok?crc32(reinterpret_cast<uint8_t*>(samplerData),info.frames*2):0;
    if(restored)ok=ok&&wanted.fingerprint==hash;else wanted.fingerprint=hash;
    if(!ok){if(had)restoreEditor(previous);else {sampler.attach(nullptr,0,24000);std::free(samplerData);samplerData=nullptr;}resumeAudio();tell("Read failed - source kept on SD");return false;}
    if(!wanted.baseSource&&wanted.sound.start==0&&wanted.sound.end==wanted.frames&&wanted.sound.mode==SampleMode::Loop&&!wanted.sound.loopRegion)wanted.baseSource=identifySampleBase(samplerData,wanted.frames,wanted.rate);
    samplePatch=wanted;toneNav.setBasic(wanted.baseSource!=0);samplerFileAction=0;sampler.attach(samplerData,wanted.frames,wanted.rate);samplerUnsaved=!restored;samplerUndoReady=false;samplerBoundSlot=-1;samplerLastMidi=0;publish();rebuildSampleWave();resumeAudio();
    samplerPage=SamplerPage::Play;samplerEditing=false;dirty=true;tell("SD sample ready");return true;
}
void listSampleFiles() {
    if(unsigned(samplerPage)<4) samplerReturn=samplerPage;
    sampleSilence();samplerFiles=0;samplerFile=0;
    if(!sdOK) sdOK=SD.begin(12,SPI,4000000) && SD.cardType()!=CARD_NONE;
    if(sdOK) {
        File directory=SD.open("/PocketSynth/Samples");
        if(directory && directory.isDirectory()) {
            unsigned visited=0;
            for(File file=directory.openNextFile();file;file=directory.openNextFile()) {
                const char* name=file.name();const char* slash=std::strrchr(name,'/');if(slash)name=slash+1;
                unsigned n=std::strlen(name);
                if(!file.isDirectory() && n>4 && n<sizeof samplerFileNames[0] && name[n-4]=='.'
                    && (name[n-3]=='w'||name[n-3]=='W') && (name[n-2]=='a'||name[n-2]=='A') && (name[n-1]=='v'||name[n-1]=='V')) {
                    std::strcpy(samplerFileNames[samplerFiles++],name);
                }
                file.close();if(samplerFiles==24 || ++visited==256)break;
            }
        }
        directory.close();
    }
    samplerPage=SamplerPage::Browse;dirty=true;
    tell(sdOK?"WAV: PCM16 mono/stereo 8-48k":"Insert SD to use sampler");
}
void openSamplePresets(bool save) {
    if(unsigned(samplerPage)<4) samplerReturn=samplerPage;
    sampleSilence();samplerSaving=save;samplerConfirm=false;refreshSampleSlots();
    snprintf(samplerName,sizeof samplerName,"%s",samplePatch.title);
    samplerPage=SamplerPage::Presets;dirty=true;
}

__attribute__((noinline)) bool saveSamplePreset(){
    if(!ensureSamplerSD()||!samplerData){tell("SD and sample required");return false;}
    if(!checkpointEditor())return false;SamplePatch record=samplePatch;snprintf(record.title,sizeof record.title,"%s",samplerName);
    if(!validPatch(record)){tell("Invalid sampler settings");return false;}
    // No PCM allocation and no audio pause needed to write a small SD record.
    bool ok=writeSampleSlot(samplerSlot,record);
    if(ok){samplePatch=record;samplerBoundSlot=int(samplerSlot);samplerUnsaved=false;}
    samplerConfirm=false;refreshSampleSlots();
    Serial.printf("SAMPLE_SAVE_SD slot=%u ok=%u heap=%u\n",samplerSlot+1,unsigned(ok),ESP.getFreeHeap());
    tell(ok?"SAVED ON SD - verified":"SD sound write failed - old kept");return ok;
}
bool writeSamplerWork(const SamplePatch& patch){
    char final[64],temp[64],backup[64];snprintf(final,sizeof final,"%s/WORK.psmp",samplerSoundDirectory);snprintf(temp,sizeof temp,"%s/WORK.tmp",samplerSoundDirectory);snprintf(backup,sizeof backup,"%s/WORK.bak",samplerSoundDirectory);
    SD.mkdir("/PocketSynth");SD.mkdir(samplerSoundDirectory);return writeSoundRecord(soundSD,final,temp,backup,patch);
}
bool leaveSampler(){
    if(!stopTakePreview())return false;
    if(!samplerData)return true;
    if(!ensureSamplerSD()||!checkpointEditor())return false;
    SD.mkdir("/PocketSynth");SD.mkdir("/PocketSynth/Sounds");
    if(!writeSamplerWork(samplePatch)){tell("SD working sound write failed");return false;}
    if(!pauseForFlash())return false;sampler.stop();sampler.attach(nullptr,0,24000);
    auto* old=samplerData;samplerData=nullptr;releaseSamplePCM(old);samplerUndoReady=false;publish();resumeAudio();return true;
}
bool readSamplerWork(SamplePatch& patch){
    char final[64],backup[64];snprintf(final,sizeof final,"%s/WORK.psmp",samplerSoundDirectory);snprintf(backup,sizeof backup,"%s/WORK.bak",samplerSoundDirectory);
    return readSoundRecord(soundSD,final,patch)||readSoundRecord(soundSD,backup,patch);
}
void openSamplerSessions(){
    samplerWorkAvailable=readSamplerWork(samplerWorkInfo);refreshSampleSlots();samplerLoadChoice=0;
    samplerPage=SamplerPage::SessionLoad;samplerEditing=false;dirty=true;
}
__attribute__((noinline)) bool newSamplerSession(){
    if(samplerData&&!leaveSampler())return false;
    samplePatch=SamplePatch{};samplerBoundSlot=-1;samplerUnsaved=false;samplerUndoReady=false;samplerLastMidi=0;
    samplerField=0;toneNav=ToneNavigation{};samplerEditing=false;samplerPage=SamplerPage::Files;samplerFileAction=3;
    rebuildSampleWave();sampleSilence();tell("NEW SESSION - RECORD OR OPEN WAV");return true;
}
__attribute__((noinline)) bool createSamplerBase(unsigned kind){
    if(kind>=unsigned(SampleBase::Count)||!prepareEditorWrite())return false;
    samplePatch=SamplePatch{};snprintf(samplePatch.title,sizeof samplePatch.title,"%s",sampleBaseName(kind));std::strcpy(samplePatch.path,"ram:base");
    samplePatch.baseSource=kind+1;samplePatch.frames=SampleBaseFrames;samplePatch.rate=SampleBaseRate;samplePatch.sound=sampleBaseSettings();
    generateSampleBase(samplerData,samplePatch.frames,SampleBase(kind),serviceKeyboardHardware);
    samplePatch.fingerprint=crc32(reinterpret_cast<uint8_t*>(samplerData),samplePatch.frames*2);
    sampler.attach(samplerData,samplePatch.frames,samplePatch.rate);samplerBoundSlot=-1;samplerUnsaved=true;samplerUndoReady=false;samplerLastMidi=0;
    toneNav=ToneNavigation{};toneNav.setBasic(true);samplerFileAction=0;samplerField=0;samplerPage=SamplerPage::Play;samplerEditing=false;publish();rebuildSampleWave();resumeAudio();
    if(!exportSampleWav()){tell("BASE READY - SD WAV SAVE FAILED");return false;}
    if(!writeSamplerWork(samplePatch)){tell("BASE WAV SAVED - WORK SAVE FAILED");return false;}
    tell("BASE READY - EDIT THEN SAVE SOUND");return true;
}
void enterSampler(){
    if(!ensureSamplerSD()){workspace=Workspace::Home;dirty=true;return;}
    if(samplerData&&!leaveSampler())return;
    if(!pauseForFlash())return;dropSynthSampleCache();resumeAudio();
    workspace=Workspace::Sampler;sampleSilence();samplerPage=SamplerPage::Start;samplerStartChoice=0;samplerEditing=false;dirty=true;
}
#include "sampler_capture.inc"
#include "sampler_synth.inc"
#include "sampler_pages.inc"
