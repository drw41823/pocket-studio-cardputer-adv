// The UI writes completed microphone blocks to SD. The audio worker is paused
// for the entire take; callbacks only publish ownership of complete buffers.
enum class MicTake : uint8_t { Idle, Countdown, Capture };
MicTake micTake=MicTake::Idle;
unsigned micExpected=32768,micPeak=0,sampleProcess=0;
constexpr unsigned MicBlock=2048;
int16_t micListen[2][MicBlock]{};
std::atomic<bool> micListenReady[2]{};
bool micListenPending[2]{};
unsigned micListenNext=0;
std::atomic<unsigned> micCompleted{0};
uint32_t micDeadline=0;
unsigned micDelaySeconds=0,micCountdownShown=0;
uint32_t micCountdownAt=0;
File micFile;
bool micToSD=false,micWriteOK=true;
char micPath[96]{},micTemporary[96]{},recordedTakePath[96]{};
WavInfo recordedTake;
unsigned takeStart=0,takeEnd=0,takeField=0;
TakeWave takeWave;File takeWaveFile;bool takeWaveError=false;uint32_t takeMoveBegan=0;unsigned takeLastStep=0;
uint8_t sampleVerify[512];
unsigned sampleExportNumber=0;

void micReleased(void*,void* data,size_t length) {
    for(unsigned i=0;i<2;++i)if(data==micListen[i]&&length==MicBlock){micListenReady[i].store(true,std::memory_order_release);return;}
}
int16_t* reserveSample(unsigned bytes,bool){if(ESP.getFreeHeap()<uint64_t(bytes)+32768)return nullptr;return static_cast<int16_t*>(std::malloc(bytes));}
bool newTakePaths(char* path,char* temporary) {
    SD.mkdir("/PocketSynth");SD.mkdir("/PocketSynth/Samples");
    for(unsigned tries=0;tries<9999;++tries) {
        sampleExportNumber=sampleExportNumber%9999+1;
        snprintf(path,96,"/PocketSynth/Samples/TAKE%04u.wav",sampleExportNumber);
        snprintf(temporary,96,"/PocketSynth/Samples/TAKE%04u.tmp",sampleExportNumber);
        if(!SD.exists(path)&&!SD.exists(temporary))return true;
    }
    return false;
}
struct TakePreviewState {
    AudioStream stream;File file;int16_t decoded[512];unsigned start=0,end=0,at=0;
    float gain=1;bool prebuffer=true;uint32_t restartAt=0,lastDraw=0;
};
TakePreviewState* takePreview=nullptr;bool takePreviewHold=false,takePreviewCheck=false;
bool startTakePreview();
__attribute__((noinline,noclone)) void renderTakePreview(int16_t* out,unsigned count){
    if(takePreview)renderTakeStream(takePreview->stream,out,count);else std::fill(out,out+count,0);
}
bool stopTakePreview(){
    takePreviewHold=false;
    if(!takePreview)return true;
    takePreview->stream.playing=false;takePreview->restartAt=0;
    if(!pauseForFlash())return false;
    takePreviewRouting.store(false,std::memory_order_release);auto* old=takePreview;takePreview=nullptr;delete old;resumeAudio();dirty=true;return true;
}
__attribute__((noinline)) void tickTakePreview(){
    if(!takePreview)return;
    if(!takePreviewCheck&&(workspace!=Workspace::Sampler||samplerPage!=SamplerPage::TakeTrim)){stopTakePreview();return;}
    auto& p=*takePreview;
    if(p.restartAt){if(int32_t(millis()-p.restartAt)>=0)startTakePreview();return;}
    p.stream.volume=takePreviewCheck?65:std::min(100u,unsigned(input.song.drums.volume+1)/2);
    for(unsigned batch=0;batch<4&&p.stream.free()>=512&&p.at<p.end;++batch){
        WavInfo part=recordedTake;part.frames=std::min(512u,p.end-p.at);part.offset=recordedTake.offset+p.at*recordedTake.channels*2;
        if(!decodeWav(sampleReadAt,&p.file,part,p.decoded,512)){stopTakePreview();tell("Preview SD read failed");return;}
        for(unsigned i=0;i<part.frames;++i)p.decoded[i]=int16_t(std::max(-24000.f,std::min(24000.f,p.decoded[i]*p.gain)));
        if(!p.stream.push(p.decoded,part.frames)){stopTakePreview();tell("Preview buffer error");return;}
        p.at+=part.frames;serviceKeyboardHardware();
    }
    if(p.at==p.end)p.stream.eof=true;
    if(p.prebuffer&&(p.stream.size()>=4096||p.stream.eof.load())){p.prebuffer=false;p.stream.playing=p.stream.size()>0;dirty=true;}
    if(p.stream.finished.exchange(false)){stopTakePreview();return;}
    if(millis()-p.lastDraw>=75){p.lastDraw=millis();dirty=true;}
}
__attribute__((noinline)) bool startTakePreview(){
    if(takeStart>=takeEnd||takeEnd>recordedTake.frames){tell("Select a preview range");return false;}
    sampleSilence();if(!pauseForFlash())return false;
    if(!takePreview&&ESP.getFreeHeap()>sizeof(TakePreviewState)+32768)takePreview=new(std::nothrow)TakePreviewState;
    if(!takePreview){resumeAudio();tell("Not enough preview RAM");return false;}
    auto& p=*takePreview;p.stream.playing=false;p.file.close();p.restartAt=0;
    p.file=SD.open(recordedTakePath,FILE_READ);WavInfo info;
    bool ok=p.file&&inspectWav(sampleReadAt,&p.file,p.file.size(),info,0xffffffffu)==WavError::None&&info.frames==recordedTake.frames&&info.rate==recordedTake.rate&&info.channels==recordedTake.channels&&info.offset==recordedTake.offset;
    if(!ok){takePreviewRouting=false;auto* old=takePreview;takePreview=nullptr;delete old;resumeAudio();tell("Preview WAV unavailable");return false;}
    p.start=p.at=takeStart;p.end=takeEnd;p.prebuffer=true;p.stream.reset(recordedTake.rate);p.lastDraw=0;
    unsigned peak=0;
    for(unsigned i=0;i<116;++i)if(uint64_t(i+1)*recordedTake.frames>uint64_t(takeStart)*116&&uint64_t(i)*recordedTake.frames<uint64_t(takeEnd)*116)
        peak=std::max(peak,std::max(unsigned(std::abs(int(takeWave.low[i]))),unsigned(std::abs(int(takeWave.high[i])))));
    p.gain=.4f*(takeWave.done==takeWave.frames&&peak?sampleLevelGain(peak):1.f);
    takePreviewRouting.store(true,std::memory_order_release);resumeAudio();tickTakePreview();dirty=true;return true;
}
void updateTakePreviewRange(){
    if(!takePreview)return;takePreview->stream.playing=false;takePreview->prebuffer=false;takePreview->restartAt=millis()+160;dirty=true;
}
void closeTakeWave(){takeWaveFile.close();}
__attribute__((noinline)) void tickTakeWave(){
    if(!takeWaveFile||takePreviewRouting.load())return;
    if(samplerPage!=SamplerPage::TakeTrim){closeTakeWave();return;}
    unsigned frameBytes=recordedTake.channels*2;
    for(unsigned block=0;block<8&&takeWave.done<takeWave.frames;++block){
        unsigned count=std::min(unsigned(sizeof sampleVerify)/frameBytes,takeWave.frames-takeWave.done),bytes=count*frameBytes;
        if(takeWaveFile.read(sampleVerify,bytes)!=int(bytes)){takeWaveError=true;closeTakeWave();dirty=true;return;}
        for(unsigned i=0;i<count;++i){int sample=int16_t(wav16(sampleVerify+i*frameBytes));if(frameBytes==4)sample=(sample+int16_t(wav16(sampleVerify+i*frameBytes+2)))/2;takeWave.append(int16_t(sample));}
        serviceKeyboardHardware();
    }
    if(takeWave.done==takeWave.frames)closeTakeWave();dirty=true;
}
bool openRecordedTake(const char* path) {
    if(!stopTakePreview())return false;
    File file=SD.open(path,FILE_READ);WavInfo info;
    bool ok=file&&file.size()<=0xffffffffu&&inspectWav(sampleReadAt,&file,uint32_t(file.size()),info,0xffffffffu)==WavError::None;
    if(!ok){file.close();tell("Recorded WAV could not be opened");return false;}
    snprintf(recordedTakePath,sizeof recordedTakePath,"%s",path);recordedTake=info;
    closeTakeWave();takeWaveFile=file;takeWaveError=!takeWaveFile.seek(info.offset);if(takeWaveError)closeTakeWave();
    takeWave.begin(info.frames);takeStart=0;takeEnd=std::min(32768u,unsigned(info.frames));takeField=info.frames>32768?2:0;takeMoveBegan=millis();takeLastStep=takeTrimStep(info.frames,info.rate,0,false,false,false);
    samplerPage=SamplerPage::TakeTrim;dirty=true;return true;
}
__attribute__((noinline)) bool loadTakeWindow(){
    WavInfo window;if(!wavWindow(recordedTake,takeStart,takeEnd,window)){tell("Select 32-32768 frames to load");return false;}
    if(!stopTakePreview())return false;closeTakeWave();
    File file=SD.open(recordedTakePath,FILE_READ);WavInfo current;
    bool ok=file&&file.size()<=0xffffffffu&&inspectWav(sampleReadAt,&file,uint32_t(file.size()),current,0xffffffffu)==WavError::None
        &&current.frames==recordedTake.frames&&current.rate==recordedTake.rate&&current.channels==recordedTake.channels&&current.offset==recordedTake.offset;
    if(!ok){file.close();tell("Take read failed - original kept");return false;}
    if(!prepareEditorWrite()){file.close();return false;}SamplePatch previous=samplePatch;
    ok=decodeWav(sampleReadAt,&file,window,samplerData,32768);file.close();
    if(!ok){if(previous.frames)restoreEditor(previous);resumeAudio();tell("Take read failed - source on SD");return false;}
    SamplePatch next;std::strcpy(next.title,"MIC CLIP");std::strcpy(next.path,"ram:edit");next.frames=window.frames;next.rate=window.rate;
    next.sound.end=window.frames;next.sound.attack=0;next.sound.decay=0;next.sound.sustain=100;next.sound.release=30;next.sound.mode=SampleMode::OneShot;
    next.fingerprint=crc32(reinterpret_cast<uint8_t*>(samplerData),window.frames*2);samplePatch=next;toneNav.setBasic(false);samplerFileAction=0;
    sampler.attach(samplerData,next.frames,next.rate);sampleControl.keys=0;++sampleControl.panic;samplerUnsaved=true;samplerUndoReady=false;samplerBoundSlot=-1;
    publish();rebuildSampleWave();resumeAudio();
    if(!exportSampleWav())return false;
    samplerPage=SamplerPage::Trim;samplerEditing=true;dirty=true;tell("Clip on SD - original take kept");return true;
}
bool consumeMicBlocks() {
    while(micListenPending[micListenNext]&&micListenReady[micListenNext].exchange(false,std::memory_order_acquire)) {
        unsigned which=micListenNext;micListenPending[which]=false;micListenNext^=1;
        unsigned completed=micCompleted.load(),count=MicBlock;

        if(micToSD) {
            // Keep RIFF/data sizes within the 32-bit WAV container.
            if(completed>(0xffffffffu-44)/2-count||micFile.write(reinterpret_cast<uint8_t*>(micListen[which]),count*2)!=count*2){micWriteOK=false;return false;}
        }
        micCompleted=completed+count;micPeak=samplePeak(micListen[which],count);micDeadline=millis()+4000;dirty=true;
    }
    return true;
}
__attribute__((noinline)) void finishSampleRecording(bool keep) {
    if(micTake==MicTake::Idle)return;
    if(micTake==MicTake::Countdown){micTake=MicTake::Idle;dirty=true;return;}
    M5Cardputer.Mic.end(); // Partial blocks are deliberately not exposed by the SDK.
    M5Cardputer.Mic.setBufferReleaseCallback(nullptr,nullptr);
    if(keep&&micWriteOK)consumeMicBlocks();
    micTake=MicTake::Idle;
    unsigned completed=micCompleted.load();bool retained=keep&&completed>=32;
    bool saved=false;
    if(micToSD) {
        // Publish only when the actual file exactly matches the complete frames.
        // A short write leaves its temporary file available for recovery.
        uint8_t header[44];wav16Header(header,completed,24000);
        bool ok=retained&&micFile.seek(0)&&micFile.write(header,sizeof header)==sizeof header;
        micFile.flush();micFile.close();
        File check=SD.open(micTemporary,FILE_READ);WavInfo info;
        ok=ok&&check&&uint64_t(check.size())==44+uint64_t(completed)*2
            &&inspectWav(sampleReadAt,&check,uint32_t(check.size()),info,0xffffffffu)==WavError::None&&info.frames==completed&&info.rate==24000&&info.channels==1;
        check.close();
        if(ok)saved=!SD.exists(micPath)&&SD.rename(micTemporary,micPath);
        if(!keep)SD.remove(micTemporary);
        retained=saved;
    }
    bool ready=M5Cardputer.Speaker.begin();
    if(ready)M5Cardputer.Speaker.setBufferReleaseCallback(nullptr,bufferReleased);
    audioReady=ready;if(ready)resumeAudio();
    if(ready&&saved&&openRecordedTake(micPath)) {
        return;
    }
    sampleResult(ready&&retained,!ready?"Audio restart failed - reboot":retained?"TAKE SAVED ON SD":!keep?"Cancelled - previous sample kept":micToSD?"SD write failed - TMP kept":"No complete audio captured");
    samplerResultReturn=SamplerPage::Record;dirty=true;
}
__attribute__((noinline)) bool startSampleRecording() {
    if(!stopTakePreview())return false;
    if(!audioReady.load()){sampleResult(false,"Audio not ready - reboot");return false;}
    if(!ensureSamplerSD()){sampleResult(false,"SD required for recording");return false;}
    sampleSilence();micWriteOK=true;micToSD=true;
    if(!micToSD)micToSD=sdOK=SD.begin(12,SPI,4000000)&&SD.cardType()!=CARD_NONE;
    if(micToSD) {
        if(!newTakePaths(micPath,micTemporary)){sampleResult(false,"No free TAKE file name");return false;}
        micFile=SD.open(micTemporary,FILE_WRITE);uint8_t header[44];wav16Header(header,0,24000);
        if(!micFile||micFile.write(header,sizeof header)!=sizeof header){micFile.close();sampleResult(false,"Cannot create SD recording");return false;}
    }
    if(!pauseForFlash()){micFile.close();if(micToSD)SD.remove(micTemporary);return false;}
    micCompleted=0;micPeak=micListenNext=0;for(unsigned i=0;i<2;++i){micListenPending[i]=false;micListenReady[i]=false;}
    M5Cardputer.Speaker.end();
    auto config=M5Cardputer.Mic.config();config.sample_rate=24000;config.stereo=false;
    M5Cardputer.Mic.config(config);M5Cardputer.Mic.setBufferReleaseCallback(nullptr,micReleased);
    micTake=MicTake::Capture;micDeadline=millis()+6000;
    if(!M5Cardputer.Mic.begin()){finishSampleRecording(false);sampleResult(false,"Microphone start failed");return false;}
    dirty=true;return true;
}
void requestSampleRecording() {
    if(!micDelaySeconds){startSampleRecording();return;}
    sampleSilence();micTake=MicTake::Countdown;micCountdownShown=micDelaySeconds;
    micCountdownAt=millis()+micDelaySeconds*1000;dirty=true;
}
void tickSampleRecording() {
    if(micTake==MicTake::Idle)return;
    if(micTake==MicTake::Countdown) {
        int32_t remaining=int32_t(micCountdownAt-millis());
        if(remaining<=0){micTake=MicTake::Idle;startSampleRecording();return;}
        unsigned seconds=(unsigned(remaining)+999)/1000;
        if(seconds!=micCountdownShown){micCountdownShown=seconds;dirty=true;}
        return;
    }
    if(!consumeMicBlocks()){finishSampleRecording(true);return;}
    if(int32_t(millis()-micDeadline)>=0){finishSampleRecording(true);return;}
    for(unsigned i=0;i<2;++i)if(!micListenPending[i]) {
        micListenPending[i]=true;
        if(!M5Cardputer.Mic.record(micListen[i],MicBlock,24000,false)){finishSampleRecording(true);return;}
    }
}
__attribute__((noinline)) bool exportSampleRange(unsigned start,unsigned end,char* output) {
    if(!samplerData||end>samplePatch.frames||start>=end||end-start<32){tell("Trim needs at least 32 frames");return false;}
    if(!ensureSamplerSD())return false;
    SD.mkdir("/PocketSynth");SD.mkdir(samplerExportDirectory);
    char path[96],temporary[96];bool available=false;
    for(unsigned tries=0;tries<256;++tries) {
        sampleExportNumber=sampleExportNumber%9999+1;
        snprintf(path,sizeof path,"%s/TAKE%04u.wav",samplerExportDirectory,sampleExportNumber);
        snprintf(temporary,sizeof temporary,"%s/TAKE%04u.tmp",samplerExportDirectory,sampleExportNumber);
        if(!SD.exists(path)&&!SD.exists(temporary)){available=true;break;}
    }
    if(!available){tell("No free TAKE file name");return false;}
    sampleSilence();
    File file=SD.open(temporary,FILE_WRITE);bool created=bool(file),ok=created;
    uint8_t header[44];wav16Header(header,end-start,samplePatch.rate);
    if(ok)ok=file.write(header,sizeof header)==sizeof header;
    auto* data=reinterpret_cast<const uint8_t*>(samplerData+start);unsigned size=(end-start)*2;
    for(unsigned at=0;ok&&at<size;) {unsigned n=std::min(1024u,size-at);ok=file.write(data+at,n)==n;at+=n;}
    file.flush();file.close();
    WavInfo info;File check=SD.open(temporary,FILE_READ);
    if(ok)ok=check && inspectWav(sampleReadAt,&check,check.size(),info)==WavError::None
        && info.frames==end-start && info.rate==samplePatch.rate && info.channels==1;
    if(ok)ok=check.seek(info.offset);
    for(unsigned at=0;ok&&at<size;) {
        unsigned n=std::min(unsigned(sizeof sampleVerify),size-at);
        ok=check.read(sampleVerify,n)==int(n) && !std::memcmp(sampleVerify,data+at,n);at+=n;
    }
    check.close();
    if(ok)ok=!SD.exists(path) && SD.rename(temporary,path);
    if(!ok && created)SD.remove(temporary);
    if(ok)snprintf(output,96,"%s",path);
    tell(ok?"WAV exported - readback verified":"WAV export failed - take kept");return ok;
}
__attribute__((noinline)) bool exportSampleWav(){
    char path[96];if(!exportSampleRange(0,samplePatch.frames,path))return false;
    snprintf(samplePatch.path,sizeof samplePatch.path,"%s",path);samplerUnsaved=true;return true;
}
__attribute__((noinline)) bool saveTrimmedSample(){
    if(!samplerData||!ensureSamplerSD()){tell("SD and sample required");return false;}
    unsigned start=samplePatch.sound.start,end=samplePatch.sound.end;
    if(end<=start||end-start<32){tell("Trim needs at least 32 frames");return false;}
    if(!checkpointEditor())return false;SamplePatch previous=samplePatch;int bound=samplerBoundSlot;
    char path[96];if(!exportSampleRange(start,end,path))return false;
    SamplePatch next=previous;snprintf(next.path,sizeof next.path,"%s",path);next.frames=end-start;next.sound.start=0;next.sound.end=next.frames;
    // Fit in original coordinates first, then relocate surviving points into the new WAV.
    if(previous.sound.loopRegion){auto fitted=previous.sound;fitSampleLoop(fitted);next.sound.loopRegion=fitted.loopRegion;
        if(fitted.loopRegion){next.sound.loopStart=fitted.loopStart-start;next.sound.loopEnd=fitted.loopEnd-start;}}

    next.fingerprint=crc32(reinterpret_cast<const uint8_t*>(samplerData+start),next.frames*2);
    if(!loadSampleSource(path,&next)){tell("Trim WAV saved; reload failed");return false;}
    samplerBoundSlot=bound;samplerUndoPatch=previous;samplerUndoReady=true;samplerUnsaved=true;
    if(!writeSamplerWork(samplePatch)){tell("Trim WAV saved; WORK save failed");return false;}
    if(bound>=0){
        if(!writeSampleSlot(unsigned(bound),samplePatch)){tell("Trim saved; SLOT update failed");return false;}
        samplerUnsaved=false;refreshSampleSlots();
    }
    Serial.printf("SAMPLE_TRIM saved frames=%u bytes=%u slot=%d path=%s\n",samplePatch.frames,44+samplePatch.frames*2,bound+1,samplePatch.path);
    tell(bound>=0?"TRIM + SOUND SAVED ON SD":"TRIM SAVED ON SD");return true;
}
__attribute__((noinline)) void processSample(){
    if(!ensureSamplerSD()||!samplerData){sampleResult(false,"SD and sample required");return;}
    sampleSilence();if(!checkpointEditor()){sampleResult(false,message);return;}
    if(sampleProcess==4){
        if(!samplerUndoReady){sampleResult(false,"No SD undo snapshot");return;}
        SamplePatch target=samplerUndoPatch,alternate=samplePatch;
        if(!loadSampleSource(target.path,&target)){sampleResult(false,message);return;}
        samplerUndoPatch=alternate;samplerUndoReady=true;samplerUnsaved=true;sampleResult(true,"SD AUDIO UNDO / REDO READY");return;
    }
    SamplePatch previous=samplePatch;
    if(!prepareEditorWrite()){sampleResult(false,message);return;}
    editSample(samplerData+samplePatch.sound.start,samplePatch.sound.end-samplePatch.sound.start,samplePatch.rate,SampleEdit(sampleProcess));
    std::strcpy(samplePatch.path,"ram:edit");samplePatch.fingerprint=crc32(reinterpret_cast<uint8_t*>(samplerData),samplePatch.frames*2);
    sampler.attach(samplerData,samplePatch.frames,samplePatch.rate);sampleControl.keys=0;++sampleControl.panic;publish();rebuildSampleWave();resumeAudio();
    if(!exportSampleWav()){
        if(pauseForFlash()){restoreEditor(previous);resumeAudio();}sampleResult(false,"Edit SD write failed - reverted");return;
    }
    samplerUndoPatch=previous;samplerUndoReady=true;samplerUnsaved=true;sampleResult(true,"EDIT SAVED TO SD");
}
