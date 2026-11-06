// A saved sampler sound is a sound source for S1-S3, not another sequencer.
// PCM can be shared by the editor, undo buffer and several synth tracks.
bool synthSlotReferenced(unsigned slot) {
    for(const auto& track:input.song.tracks)if(track.wave==slot+4)return true;
    return false;
}
void updateSynthSoundSlot(unsigned slot) {
    if(slot>=4||!synthSlotReferenced(slot)||!samplerData)return;
    auto* old=const_cast<int16_t*>(synthSamples[slot].data);
    auto& sound=synthSamples[slot];sound.data=samplerData;sound.frames=samplePatch.frames;sound.rate=samplePatch.rate;sound.sound=samplePatch.sound;++sound.generation;
    synthSampleInfo[slot]=samplePatch;publish();releaseSamplePCM(old);
}
__attribute__((noinline)) bool loadSynthSample(unsigned slot) {
    if(slot>=4)return false;if(synthSamples[slot].data)return true;
    SamplePatch patch;
    if(!readSampleSlot(slot,patch)){tell("Sample sound slot empty / invalid");return false;}
    int16_t* candidate=nullptr;bool shared=samplerData&&samplePatch.frames==patch.frames&&samplePatch.rate==patch.rate&&samplePatch.fingerprint==patch.fingerprint;
    if(shared)candidate=samplerData;
    else {
        candidate=reserveSample(patch.frames*2);
        if(!candidate){tell("Sample sources exceed free RAM");return false;}
        bool ok=true;
        if(!std::strcmp(patch.path,"builtin:pluck")) {
            if(patch.frames!=12000||patch.rate!=24000)ok=false;
            else for(unsigned i=0;i<patch.frames;++i) {
                float t=float(i)/patch.rate,phase=2*Pi*261.625565f*t;
                float value=(std::sin(phase)+.4f*std::sin(phase*2.003f)+.2f*std::sin(phase*4.01f))*std::exp(-t*7.f);
                candidate[i]=int16_t(value*10000.f*std::min(1.f,t*500.f));
            }
        } else {
            if(!sdOK)sdOK=SD.begin(12,SPI,4000000)&&SD.cardType()!=CARD_NONE;
            File file=sdOK?SD.open(patch.path,FILE_READ):File{};WavInfo info;
            ok=file&&file.size()<=0xffffffffu&&inspectWav(sampleReadAt,&file,uint32_t(file.size()),info)==WavError::None
                &&info.frames==patch.frames&&info.rate==patch.rate&&decodeWav(sampleReadAt,&file,info,candidate,patch.frames);
            file.close();
        }
        if(ok)ok=crc32(reinterpret_cast<const uint8_t*>(candidate),patch.frames*2)==patch.fingerprint;
        if(!ok){std::free(candidate);tell("Sample WAV missing or changed");return false;}
    }
    if(!pauseForFlash()){if(!shared)std::free(candidate);return false;}
    auto& source=synthSamples[slot];source.data=candidate;source.frames=patch.frames;source.rate=patch.rate;source.sound=patch.sound;++source.generation;
    synthSampleInfo[slot]=patch;publish();resumeAudio();return true;
}
void refreshSynthSamples() {
    if(workspace!=Workspace::Synth)return;
    static unsigned previous[3]={255,255,255};bool changed=false;
    for(unsigned i=0;i<3;++i)if(previous[i]!=input.song.tracks[i].wave){previous[i]=input.song.tracks[i].wave;changed=true;}
    if(!changed)return;
    bool unused=false;for(unsigned i=0;i<4;++i)unused|=synthSamples[i].data&&!synthSlotReferenced(i);
    if(unused&&pauseForFlash()) {
        for(unsigned i=0;i<4;++i)if(synthSamples[i].data&&!synthSlotReferenced(i)) {
            auto* old=const_cast<int16_t*>(synthSamples[i].data);synthSamples[i].data=nullptr;++synthSamples[i].generation;releaseSamplePCM(old);
        }
        publish();resumeAudio();
    }
    for(const auto& track:input.song.tracks)if(track.wave>=4)loadSynthSample(track.wave-4);
}
__attribute__((noinline)) void nextSynthWave(unsigned track) {
    unsigned current=input.song.tracks[track].wave;
    for(unsigned attempts=0;attempts<8;++attempts) {
        current=(current+1)%8;
        if(current<4||loadSynthSample(current-4)){input.song.tracks[track].wave=current;return;}
    }
}
