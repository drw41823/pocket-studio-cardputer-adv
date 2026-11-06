// Explicit serial V runs a non-audible production-path check in a fresh,
// private SD directory. Never writes to user sound slots or records the mic.
__attribute__((noinline)) bool checkHeldSample(){
    SamplerEngine test;test.attach(samplerData,samplePatch.frames,samplePatch.rate);
    SampleControl c;c.sound=samplePatch.sound;c.sound.mode=SampleMode::Loop;c.sound.attack=0;c.sound.decay=0;c.sound.sustain=100;c.sound.release=30;c.keys=1;
    test.apply(c);int16_t out[128];unsigned peak=0,worst=0;
    for(unsigned b=0;b<16*SampleRate/128;++b){uint32_t begin=micros();test.render(out,128);worst=std::max(worst,unsigned(micros()-begin));if(b+100>16*SampleRate/128)peak=std::max(peak,samplePeak(out,128));}
    Serial.printf("SAMPLER_FX cutoff=%u resonance=%u drive=%u bits=%u peak=%u block_max_us=%u\n",c.sound.cutoff,c.sound.resonance,c.sound.drive,c.sound.bits,peak,worst);
    if(test.active()!=1||!peak)return false;
    c.keys=0;test.apply(c);for(unsigned b=0;b<100;++b)test.render(out,128);
    if(test.active())return false;
    c.keys=15;c.sound.oscA=4;c.sound.oscB=3;c.sound.oscMix=50;c.sound.oscTune=7;c.sound.lfoTarget=2;c.sound.lfoRate=200;c.sound.lfoDepth=100;c.sound.pitchEnv=48;c.sound.pitchDecay=2000;c.sound.filterEnv=-80;c.sound.filterDecay=2000;test.apply(c);worst=0;
    for(unsigned i=0;i<256;++i){uint32_t begin=micros();test.render(out,128);worst=std::max(worst,unsigned(micros()-begin));}
    Serial.printf("SAMPLER_LOOP four_voices=%u block_max_us=%u in=%u out=%u fade=%u\n",test.active(),worst,c.sound.loopStart,c.sound.loopEnd,c.sound.loopFade);
    if(test.active()!=4||worst>=5000)return false;c.keys=0;test.apply(c);for(unsigned b=0;b<100;++b)test.render(out,128);
    return test.active()==0;
}
// Entire production workstation plus sample adapters, allocated only for QA.
struct StudioOscCheck {
    Workstation engine;SamplerEngine voices[3];Input state;SampleSettings sound;bool attached[3]{};unsigned applyUs=0,renderUs=0;
    static void apply(void* context,unsigned track,unsigned,const Controls& controls){auto& rig=*static_cast<StudioOscCheck*>(context);uint32_t begin=micros();
        rig.voices[track].setPolyphony(4);
        if(!rig.attached[track]){rig.voices[track].attach(samplerData,samplePatch.frames,samplePatch.rate);rig.attached[track]=true;}
        SampleControl c;c.sound=rig.sound;c.master=controls.volume;c.panic=controls.panic;rig.voices[track].applyMidi(c,controls.midiNotes,controls.retriggerNotes);rig.applyUs+=micros()-begin;}
    static void render(void* context,unsigned track,int16_t* out,unsigned n){auto& rig=*static_cast<StudioOscCheck*>(context);uint32_t begin=micros();rig.voices[track].render(out,n);rig.renderUs+=micros()-begin;}
};
__attribute__((noinline)) bool checkStudioOscillators(){
    // QA preserves the same 32KB reserve as persistent sound data.
    if(ESP.getFreeHeap()<sizeof(StudioOscCheck)+32768u){Serial.printf("STUDIO_OSC FAIL rig_memory=%u need=%u\n",ESP.getFreeHeap(),unsigned(sizeof(StudioOscCheck)+32768));return false;}
    std::unique_ptr<StudioOscCheck> rig(new(std::nothrow) StudioOscCheck);if(!rig)return false;
    rig->engine.init();rig->engine.onSampleTrack(rig.get(),StudioOscCheck::apply,StudioOscCheck::render);
    rig->sound=samplePatch.sound;rig->sound.mode=SampleMode::Loop;rig->sound.attack=0;rig->sound.decay=0;rig->sound.sustain=100;
    rig->sound.oscA=4;rig->sound.oscB=3;rig->sound.oscMix=50;rig->sound.oscTune=7;rig->sound.lfoTarget=2;rig->sound.lfoShape=3;rig->sound.lfoRate=200;rig->sound.lfoDepth=100;
    rig->state.playing=true;rig->state.epoch=1;rig->state.song.drums.bpm=120;rig->state.song.drums.length=16;
    for(auto& bits:rig->state.song.drums.steps)bits=0x1111;
    for(auto& track:rig->state.song.tracks){track.wave=4;track.count=4;for(unsigned i=0;i<4;++i){auto& e=track.events[i];e.start=0;e.duration=rig->state.song.span();e.midi=i==0?60:i==1?64:i==2?67:71;e.velocity=100;}}
    int16_t out[128];unsigned worst=0,over=0,peak=0,applyWorst=0,renderWorst=0,otherWorst=0;
    for(unsigned i=0;i<256;++i){rig->applyUs=rig->renderUs=0;uint32_t begin=micros();rig->engine.apply(rig->state);rig->engine.render(out,128);unsigned elapsed=micros()-begin;worst=std::max(worst,elapsed);over+=elapsed>=5333;applyWorst=std::max(applyWorst,rig->applyUs);renderWorst=std::max(renderWorst,rig->renderUs);otherWorst=std::max(otherWorst,elapsed>rig->applyUs+rig->renderUs?elapsed-rig->applyUs-rig->renderUs:0);peak=std::max(peak,samplePeak(out,128));if(!(i%8))serviceKeyboardHardware();}
    bool ok=peak&&rig->voices[0].active()==4&&rig->voices[1].active()==4&&rig->voices[2].active()==4&&!over;
    Serial.printf("STUDIO_OSC %s twelve_voices=1 drums=8 block_max_us=%u over_budget=%u heap_rig=%u apply_us=%u render_us=%u other_us=%u\n",ok?"PASS":"FAIL",worst,over,unsigned(sizeof(StudioOscCheck)),applyWorst,renderWorst,otherWorst);return ok;
}
__attribute__((noinline)) bool writeCheckFixture(const char* path){
    File f=SD.open(path,FILE_WRITE);uint8_t header[44];wav16Header(header,32768,24000);
    if(!f||f.write(header,sizeof header)!=sizeof header){f.close();return false;}
    int16_t block[128];for(unsigned i=0;i<128;++i)block[i]=int16_t((int(i%64)-32)*100);
    for(unsigned i=0;i<256;++i)if(f.write(reinterpret_cast<uint8_t*>(block),sizeof block)!=sizeof block){f.close();return false;}
    f.flush();f.close();return true;
}
__attribute__((noinline)) bool checkSavedTrim(){
    samplePatch.sound.start=4099;samplePatch.sound.end=20483;
    unsigned expected=crc32(reinterpret_cast<const uint8_t*>(samplerData+4099),16384*2);auto* address=samplerData;samplerBoundSlot=0;
    if(!saveTrimmedSample())return false;SamplePatch saved;
    File cropped=SD.open(samplePatch.path,FILE_READ);unsigned bytes=cropped?cropped.size():0;cropped.close();
    bool ok=samplerData==address&&samplePatch.frames==16384&&samplePatch.sound.start==0&&samplePatch.sound.end==16384&&samplePatch.fingerprint==expected&&bytes==44+16384*2
        &&samplePatch.sound.loopRegion&&samplePatch.sound.loopStart==3901&&samplePatch.sound.loopEnd==13901
        &&readSampleSlot(0,saved)&&saved.sound.loopRegion&&saved.sound.loopStart==3901&&saved.sound.loopEnd==13901&&saved.frames==16384&&saved.fingerprint==expected&&saved.sound.bits==7&&!std::strcmp(saved.path,samplePatch.path)&&samplerUndoReady;
    if(ok)ok=loadSampleSource(saved.path,&saved)&&samplePatch.fingerprint==expected;
    Serial.printf("SAMPLER_TRIM %s frames=%u file_bytes=%u pcm_reused=%u\n",ok?"PASS":"FAIL",samplePatch.frames,bytes,unsigned(samplerData==address));return ok;
}
__attribute__((noinline)) bool checkLegacySoundRecord(){
    char final[64],temporary[64],backup[64];soundPaths(0,final,temporary,backup);
    uint8_t bytes[SamplePatchBytes];encodeSamplePatch(samplePatch,bytes);bytes[5]='3';patch32(bytes+156,crc32(bytes,156));
    if(!soundSD.write(final,bytes,160))return false;SamplePatch legacy;
    bool ok=readSampleSlot(0,legacy)&&legacy.frames==samplePatch.frames&&!legacy.sound.loopRegion&&legacy.sound.loopFade==64;
    Serial.printf("SAMPLER_LEGACY %s PSMP03=160 full_loop=1\n",ok?"PASS":"FAIL");return ok;
}
__attribute__((noinline)) bool checkSamplerStartup(){
    SamplePatch original;if(!readSampleSlot(0,original))return false;auto* address=samplerData;
    for(unsigned kind=0;kind<5;++kind){
        if(!createSamplerBase(kind)||samplerData!=address)return false;SamplePatch work,slot;
        if(!readSamplerWork(work)||work.baseSource!=kind+1||work.frames!=SampleBaseFrames||work.rate!=SampleBaseRate||work.sound.autoLevel||work.sound.mode!=SampleMode::Loop||work.fingerprint!=samplePatch.fingerprint||std::strcmp(work.title,sampleBaseName(kind)))return false;
        if(!readSampleSlot(0,slot)||slot.fingerprint!=original.fingerprint||slot.frames!=original.frames||std::strcmp(slot.path,original.path))return false;
        if(!loadSampleSource(work.path,&work)||samplerData!=address||samplePatch.baseSource!=kind+1||!toneNav.basic||toneNav.enabled(4))return false;
        work.baseSource=0;if(!loadSampleSource(work.path,&work)||samplePatch.baseSource!=kind+1||!toneNav.basic)return false;
    }
    SamplePatch last;if(!readSamplerWork(last)||!newSamplerSession()||samplerData||samplePatch.frames||samplerPage!=SamplerPage::Files)return false;
    SamplePatch retained;if(!readSamplerWork(retained)||retained.fingerprint!=last.fingerprint||std::strcmp(last.path,retained.path))return false;
    bool ok=loadSampleSource(retained.path,&retained);Serial.printf("SAMPLER_STARTUP %s five_bases=1 reload=1 empty_new=1 saved_slot_preserved=1\n",ok?"PASS":"FAIL");return ok;
}
__attribute__((noinline)) bool checkRecordedTakeWave(const char* path){
    if(!openRecordedTake(path))return false;
    for(unsigned i=0;i<64&&takeWaveFile;++i)tickTakeWave();
    bool ok=!takeWaveError&&takeWave.done==samplePatch.frames&&takeWave.peak==samplePeak(samplerData,samplePatch.frames);
    for(unsigned x=0;ok&&x<116;++x){
        unsigned begin=unsigned((uint64_t(x)*samplePatch.frames+115)/116),end=unsigned((uint64_t(x+1)*samplePatch.frames+115)/116);
        int16_t lo=32767,hi=-32768;for(unsigned j=begin;j<end;++j){lo=std::min(lo,samplerData[j]);hi=std::max(hi,samplerData[j]);}
        ok=takeWave.low[x]==lo&&takeWave.high[x]==hi;
    }
    closeTakeWave();samplerPage=SamplerPage::Play;Serial.printf("SAMPLER_TAKE_WAVE %s frames=%u peak=%u\n",ok?"PASS":"FAIL",takeWave.done,takeWave.peak);return ok;
}
__attribute__((noinline)) bool checkRecordedTakePreview(const char* path){
    if(!openRecordedTake(path))return false;while(takeWaveFile)tickTakeWave();
    takeStart=4099;takeEnd=20483;takePreviewCheck=true;
    bool ok=startTakePreview();unsigned peak=0,position=0;int16_t out[128];
    for(unsigned block=0;ok&&takePreview&&block<1000;++block){
        tickTakePreview();if(!takePreview)break;
        renderTakePreview(out,128);peak=std::max(peak,samplePeak(out,128));position=takePreview->stream.position.load();
        if(takePreview->stream.underruns)ok=false;
    }
    ok=ok&&!takePreview&&peak>0&&position==takeEnd-takeStart;
    for(unsigned repeat=0;ok&&repeat<5;++repeat){ok=startTakePreview();if(ok){tickTakePreview();renderTakePreview(out,128);ok=stopTakePreview()&&!takePreview&&!takePreviewRouting.load();}}
    stopTakePreview();takePreviewCheck=false;samplerPage=SamplerPage::Play;
    Serial.printf("SAMPLER_TAKE_PREVIEW %s consumed=%u peak=%u repeated_stop=5\n",ok?"PASS":"FAIL",position,peak);return ok;
}
__attribute__((noinline)) void runSamplerSDCheck(){
    if(workspace!=Workspace::Home||micTake!=MicTake::Idle||samplerData){Serial.println("SAMPLER_CHECK SKIP: run after boot from Home");return;}
    if(!ensureSamplerSD()){Serial.println("SAMPLER_CHECK FAIL: SD missing");return;}
    char dir[48],source[96];bool created=false;
    for(unsigned i=1;i<1000;++i){snprintf(dir,sizeof dir,"/PocketSynth/QA%04u",i);if(!SD.exists(dir)){created=SD.mkdir(dir);break;}}
    if(!created){Serial.println("SAMPLER_CHECK FAIL: test directory");return;}
    snprintf(source,sizeof source,"%s/SOURCE.wav",dir);samplerCheckMode=true;snprintf(samplerSoundDirectory,sizeof samplerSoundDirectory,"%s",dir);samplerExportDirectory=dir;
    bool ok=[&](){
        if(!writeCheckFixture(source))return false;
        if(!loadSampleSource(source))return false;
        if(!checkLegacySoundRecord()||!checkRecordedTakeWave(source)||!checkRecordedTakePreview(source))return false;
        auto* address=samplerData;unsigned startHeap=ESP.getFreeHeap();
        samplerSlot=0;std::strcpy(samplerName,"CHECK_A");if(!saveSamplePreset())return false;
        SamplePatch saved;if(!readSampleSlot(0,saved)||std::strcmp(saved.title,"CHECK_A"))return false;
        if(!loadSampleSource(saved.path,&saved))return false;
        samplePatch.sound.oscA=4;samplePatch.sound.oscB=2;samplePatch.sound.oscMix=37;samplePatch.sound.oscTune=-12;samplePatch.sound.lfoTarget=2;samplePatch.sound.lfoShape=3;samplePatch.sound.lfoRate=173;samplePatch.sound.lfoDepth=65;samplePatch.sound.pitchEnv=-24;samplePatch.sound.pitchDecay=420;samplePatch.sound.filterEnv=60;samplePatch.sound.filterDecay=730;samplePatch.sound.transpose=3;samplePatch.sound.fine=-12;samplePatch.sound.mode=SampleMode::Loop;samplePatch.sound.release=150;samplePatch.sound.cutoff=43;samplePatch.sound.resonance=65;samplePatch.sound.drive=30;samplePatch.sound.bits=7;samplePatch.sound.loopRegion=true;samplePatch.sound.loopStart=8000;samplePatch.sound.loopEnd=18000;samplePatch.sound.loopFade=128;std::strcpy(samplerName,"CHECK_B");
        if(!saveSamplePreset()||!readSampleSlot(0,saved)||std::strcmp(saved.title,"CHECK_B")||saved.sound.mode!=SampleMode::Loop||saved.sound.release!=150||saved.sound.cutoff!=43||saved.sound.resonance!=65||saved.sound.drive!=30||saved.sound.bits!=7||!saved.sound.loopRegion||saved.sound.loopStart!=8000||saved.sound.loopEnd!=18000||saved.sound.loopFade!=128||saved.sound.pitchEnv!=-24||saved.sound.pitchDecay!=420||saved.sound.filterEnv!=60||saved.sound.filterDecay!=730||saved.sound.transpose!=3||saved.sound.fine!=-12||saved.sound.oscA!=4||saved.sound.oscB!=2||saved.sound.oscMix!=37||saved.sound.oscTune!=-12||saved.sound.lfoTarget!=2||saved.sound.lfoShape!=3||saved.sound.lfoRate!=173||saved.sound.lfoDepth!=65)return false;
        for(unsigned i=0;i<8;++i)if(!loadSampleSource(saved.path,&saved)||samplerData!=address)return false;
        for(unsigned i=0;i<50;++i){if(!pauseForFlash())return false;resumeAudio();}
        if(!checkHeldSample()||!checkStudioOscillators()||!checkSavedTrim()||!checkSamplerStartup())return false;
        unsigned endHeap=ESP.getFreeHeap();
        Serial.printf("SAMPLER_CHECK memory_start=%u memory_end=%u pcm_reused=1 held_loop=16s release_ok=1\n",startHeap,endHeap);
        return startHeap<=endHeap+4096;
    }();
    if(pauseForFlash()){sampler.stop();sampler.attach(nullptr,0,24000);auto* old=samplerData;samplerData=nullptr;releaseSamplePCM(old);samplePatch=SamplePatch{};publish();resumeAudio();}
    // This directory was created fresh by this invocation; only its QA files exist here.
    File cleanup=SD.open(dir);char path[96];
    for(File entry=cleanup.openNextFile();entry;entry=cleanup.openNextFile()){
        const char* name=entry.name();const char* slash=std::strrchr(name,'/');if(slash)name=slash+1;
        snprintf(path,sizeof path,"%s/%s",dir,name);bool regular=!entry.isDirectory();entry.close();
        if(!regular||!SD.remove(path))ok=false;
    }
    cleanup.close();
    if(!SD.rmdir(dir))ok=false;
    std::strcpy(samplerSoundDirectory,"/PocketSynth/Sounds");samplerExportDirectory="/PocketSynth/Samples";samplerCheckMode=false;samplerBoundSlot=-1;samplerUnsaved=true;samplerEditing=false;samplerUndoReady=false;samplerPage=SamplerPage::Play;dirty=true;
    Serial.printf("SAMPLER_CHECK %s %s\n",ok?"PASS":"FAIL",ok?"save overwrite reload pause held-loop cleanup":message);
}
