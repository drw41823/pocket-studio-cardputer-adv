// Source-aware keyboard navigation and drawing. Hardware audio setup is unchanged.
bool sampleNav(const uint8_t* held,char key,unsigned index) {
    if(!held[unsigned(key)]) {samplerRepeatAt[index]=0;return false;}
    auto now=millis();
    if(!oldKeys[unsigned(key)]) {samplerRepeatAt[index]=now+350;return true;}
    if(samplerRepeatAt[index] && int32_t(now-samplerRepeatAt[index])>=0) {
        samplerRepeatAt[index]=now+65;return true;
    }
    return false;
}
void sampleResult(bool ok,const char* result) {
    snprintf(samplerResult,sizeof samplerResult,"%s",result);
    samplerResultReturn=samplerPage;
    samplerResultOK=ok;samplerPage=SamplerPage::Result;dirty=true;
}
void sampleNextPage(bool reverse) {
    sampleSilence();
    samplerPage=SamplerPage(samplerNextPage(unsigned(samplerPage),reverse,samplePatch.baseSource!=0));
    samplerField=samplerPage==SamplerPage::Tone?toneNav.field():0;toneNav.focusGroups();dirty=true;
}
void runSampleAction(unsigned action) {
    if(unsigned(samplerPage)<4)samplerReturn=samplerPage;
    if(samplePatch.baseSource&&(action==4||action==8)){samplerPage=SamplerPage::Tone;toneNav.focusGroups();dirty=true;return;}
    if(action==10){sampleSilence();samplerBaseFromFiles=true;samplerBaseChoice=samplePatch.baseSource?samplePatch.baseSource-1:0;samplerPage=SamplerPage::Bases;dirty=true;return;}
    if(action==0)listSampleFiles();
    if(action==1)openSamplePresets(true);
    if(action==2)openSamplePresets(false);
    if(action>=3&&action<=5) {samplerPage=action==3?SamplerPage::Play:action==4?SamplerPage::Trim:SamplerPage::Tone;samplerField=0;}
    if(action==6) {sampleSilence();samplerPage=SamplerPage::Record;}
    if(action==7) {
        bool ok=exportSampleWav();
        if(ok) {char result[40];const char* name=std::strrchr(samplePatch.path,'/');snprintf(result,sizeof result,"WAV EXPORTED: %.17s",name?name+1:samplePatch.path);sampleResult(true,result);}
        else sampleResult(false,message);
    }
    if(action==8) {sampleSilence();sampleProcess=0;samplerPage=SamplerPage::Process;}
    if(action==9) {sampleSilence();if(!leaveSampler()){sampleResult(false,message);return;}workspace=Workspace::Home;}
    dirty=true;
}
__attribute__((noinline)) void scanWorkspace(const uint8_t* held,bool enter,bool tab,bool backspace) {
    const auto& special=M5Cardputer.Keyboard.keysState();
    auto pressed=[&](char key){return held[unsigned(key)]&&!oldKeys[unsigned(key)];};
    bool up=sampleNav(held,';',0),down=sampleNav(held,'.',1);
    bool left=sampleNav(held,',',2),right=sampleNav(held,'/',3);
    if(workspace==Workspace::Home) {
        if(up||down) {homeSelection=(homeSelection+(up?2:1))%3;dirty=true;}
        if(enter) {
            stop();blockHeld(held);
            if(homeSelection==0) {for(const auto& track:input.song.tracks)if(track.wave>=4)loadSynthSample(track.wave-4);workspace=Workspace::Synth;overview=true;}
            else if(homeSelection==1)enterSampler();else enterPlayer();
            dirty=true;
        }
        return;
    }
    if(samplePatch.baseSource&&(samplerPage==SamplerPage::Trim||samplerPage==SamplerPage::Process||samplerPage==SamplerPage::Actions)){samplerPage=SamplerPage::Tone;samplerEditing=false;toneNav.focusGroups();dirty=true;}
    if(samplerPage==SamplerPage::Start){
        if(up||down){samplerStartChoice=(samplerStartChoice+(up?2:1))%3;dirty=true;}
        if(backspace){workspace=Workspace::Home;dirty=true;}
        if(enter){
            if(!samplerStartChoice)openSamplerSessions();
            else if(samplerStartChoice==1)newSamplerSession();
            else {samplerBaseFromFiles=false;samplerBaseChoice=0;samplerPage=SamplerPage::Bases;dirty=true;}
            blockHeld(held);
        }
        sampleControl.keys=0;return;
    }
    if(samplerPage==SamplerPage::SessionLoad||samplerPage==SamplerPage::Bases){
        bool bases=samplerPage==SamplerPage::Bases;unsigned& choice=bases?samplerBaseChoice:samplerLoadChoice;
        if(up||down){choice=(choice+(up?4:1))%5;dirty=true;}
        if(backspace||tab){samplerPage=bases&&samplerBaseFromFiles?SamplerPage::Files:SamplerPage::Start;dirty=true;blockHeld(held);}
        else if(enter){
            if(bases){if(!createSamplerBase(choice)){sampleResult(false,message);samplerResultReturn=samplerData?SamplerPage::Play:SamplerPage::Bases;}}
            else {const SamplePatch* target=choice?samplerSlotValid[choice-1]?&samplerSlotInfo[choice-1]:nullptr:samplerWorkAvailable?&samplerWorkInfo:nullptr;
                if(!target)tell("NO SAVED SESSION IN THIS ENTRY");
                else if(loadSampleSource(target->path,target)){samplerBoundSlot=choice?int(choice-1):-1;}
                else {sampleResult(false,message);samplerResultReturn=SamplerPage::SessionLoad;}}
            blockHeld(held);
        }
        sampleControl.keys=0;return;
    }
    if(samplerPage==SamplerPage::Record) {
        if(micTake!=MicTake::Idle) {
            if(tab||backspace)finishSampleRecording(false);
            else if(enter)finishSampleRecording(true);
        } else if(tab) {samplerPage=SamplerPage::Files;dirty=true;}
        else if(left||right){micDelaySeconds=unsigned(std::max(0,std::min(60,int(micDelaySeconds)+(right?1:-1)*(special.shift?5:1))));dirty=true;}
        else if(enter)requestSampleRecording();
        if(enter||tab||backspace)blockHeld(held);return;
    }
    if(samplerPage==SamplerPage::TakeTrim) {
        if(tab||backspace){stopTakePreview();samplerPage=SamplerPage::Files;dirty=true;}
        else if(pressed(' ')){if(takePreview)stopTakePreview();else {takePreviewHold=false;startTakePreview();}blockHeld(held);}
        else if(pressed('e')&&!special.ctrl&&!special.fn){takePreviewHold=startTakePreview();}
        else if(up||down){takeField=(takeField+(up?2:1))%3;dirty=true;}
        else if(left||right) {
            char direction=right?'/':',';
            if(!oldKeys[unsigned(direction)])takeMoveBegan=millis();
            unsigned heldMs=millis()-takeMoveBegan;
            takeLastStep=takeTrimStep(recordedTake.frames,recordedTake.rate,heldMs,special.fn,special.shift,special.ctrl);
            moveTakeTrim(recordedTake.frames,takeField,right?int64_t(takeLastStep):-int64_t(takeLastStep),takeStart,takeEnd);
            updateTakePreviewRange();
            dirty=true;
        }
        else if(enter){if(!loadTakeWindow())sampleResult(false,message);blockHeld(held);}
        if(takePreviewHold&&!held[unsigned('e')])stopTakePreview();
        return;
    }
    if(samplerPage==SamplerPage::Rename) {
        unsigned n=std::strlen(samplerName);
        if(backspace&&n)samplerName[--n]=0;
        if(enter) {samplerPage=SamplerPage::Presets;blockHeld(held);}
        else if(tab) {snprintf(samplerName,sizeof samplerName,"%s",samplePatch.title);samplerPage=SamplerPage::Presets;blockHeld(held);}
        else if(!special.ctrl&&!special.fn) for(unsigned key=32;key<127;++key) if(pressed(char(key))&&n<15) {
            char c=char(key);if(special.shift&&c>='a'&&c<='z')c-=32;samplerName[n++]=c;samplerName[n]=0;
        }
        dirty=true;return;
    }
    if(samplerPage==SamplerPage::Result) {
        if(enter||tab||backspace) {samplerPage=samplerResultOK?SamplerPage::Play:samplerResultReturn;blockHeld(held);dirty=true;}
        return;
    }
    if(backspace) {sampleSilence();blockHeld(held);tell("Sampler stopped");}
    if(special.ctrl) {
        if(pressed('o')) {listSampleFiles();blockHeld(held);}
        if(pressed('s')) {openSamplePresets(true);blockHeld(held);}
        if(pressed('l')) {openSamplePresets(false);blockHeld(held);}
        if(pressed('m')) {sampleSilence();samplerPage=SamplerPage::Record;blockHeld(held);dirty=true;}
        if(pressed('z')) {if(samplePatch.baseSource)tell("BASE SOURCE - SHAPE WITH TONE");else {sampleProcess=4;processSample();}blockHeld(held);}
        sampleControl.keys=0;return;
    }
    if(tab) {
        sampleSilence();
        if(samplerPage==SamplerPage::Presets && samplerConfirm) samplerConfirm=false;
        else if(unsigned(samplerPage)>=4) {samplerPage=samplerReturn;samplerEditing=true;}
        else samplerEditing=false;
        blockHeld(held);dirty=true;return;
    }
    if((samplerPage==SamplerPage::Play||samplerPage==SamplerPage::Files)&&(left||right)){sampleNextPage(left);samplerEditing=false;blockHeld(held);return;}
    if((samplerPage==SamplerPage::Trim||samplerPage==SamplerPage::Tone)&&!samplerEditing){
        if(left||right){sampleNextPage(left);blockHeld(held);}
        if(enter){if(!samplerData)tell("Record or open WAV first");else {samplerEditing=true;if(samplerPage==SamplerPage::Tone)toneNav.focusGroups();}blockHeld(held);dirty=true;}
        sampleControl.keys=0;return;
    }
    if(samplerPage==SamplerPage::Tone&&backspace&&samplerEditing&&toneNav.back()){dirty=true;sampleControl.keys=0;return;}
    if(unsigned(samplerPage)<4&&backspace){samplerEditing=false;sampleControl.keys=0;blockHeld(held);dirty=true;return;}
    if(samplerPage==SamplerPage::Browse) {
        if((up||down)&&samplerFiles) {samplerFile=(samplerFile+(up?samplerFiles-1:1))%samplerFiles;dirty=true;}
        if(enter) {
            char path[96];bool ok;
            if(!samplerFiles){sampleResult(false,"No WAV files on SD - record first");return;}
            snprintf(path,sizeof path,"/PocketSynth/Samples/%s",samplerFileNames[samplerFile]);ok=loadSampleSource(path);
            if(!ok) sampleResult(false,message);
            blockHeld(held);
        }
        return;
    }
    if(samplerPage==SamplerPage::Presets) {
        if(up||down) {samplerSlot=(samplerSlot+(up?3:1))%4;samplerConfirm=false;dirty=true;}
        if(samplerSaving && pressed('n')) {samplerPage=SamplerPage::Rename;blockHeld(held);dirty=true;}
        if(enter) {
            if(samplerSaving) {
                if(samplerSlotPresent[samplerSlot]&&!samplerConfirm) {samplerConfirm=true;dirty=true;}
                else {
                    bool ok=saveSamplePreset();
                    if(ok) {char result[40];snprintf(result,sizeof result,"SAVED TO SD SLOT %u",samplerSlot+1);sampleResult(true,result);}
                    else sampleResult(false,message);
                }
            } else if(samplerSlotValid[samplerSlot]) {
                bool ok=loadSampleSource(samplerSlotInfo[samplerSlot].path,&samplerSlotInfo[samplerSlot]);
                if(ok) {samplerBoundSlot=int(samplerSlot);char result[40];snprintf(result,sizeof result,"LOADED SD SLOT %u",samplerSlot+1);sampleResult(true,result);}
                else sampleResult(false,message);
            } else sampleResult(false,"Preset empty or invalid");
            blockHeld(held);
        }
        return;
    }
    if(samplerPage==SamplerPage::Actions) {
        if(up||down) {samplerAction=(samplerAction+(up?9:1))%10;dirty=true;}
        if(enter) {runSampleAction(samplerAction);blockHeld(held);}
        return;
    }
    if(samplerPage==SamplerPage::Files) {
        if(up||down) {samplerFileAction=(samplerFileAction+(up?samplerFileCount(samplePatch.baseSource!=0)-1:1))%samplerFileCount(samplePatch.baseSource!=0);dirty=true;}
        if(enter) {runSampleAction(samplerFileActionAt(samplerFileAction,samplePatch.baseSource!=0));blockHeld(held);}
        return;
    }
    if(samplerPage==SamplerPage::Process) {
        if(up||down){sampleProcess=(sampleProcess+(up?4:1))%5;dirty=true;}
        if(enter){processSample();blockHeld(held);}
        return;
    }
    if(enter&&samplerPage==SamplerPage::Trim&&samplerField==2){
        bool ok=saveTrimmedSample();sampleResult(ok,ok?"TRIM SAVED - OUTSIDE REMOVED":message);blockHeld(held);return;
    }
    if(enter&&samplerPage==SamplerPage::Trim){samplerEditing=false;sampleSilence();blockHeld(held);dirty=true;return;}
    if(samplerPage==SamplerPage::Trim || samplerPage==SamplerPage::Tone) {
        int toneDelta=0;
        if(samplerPage==SamplerPage::Tone){toneDelta=toneNav.move(up,down,left,right,enter);samplerField=toneNav.field();if(up||down||left||right||enter)dirty=true;}
        else if(up||down){samplerField=(samplerField+(up?2:1))%3;dirty=true;}
        bool adjust=samplerPage==SamplerPage::Tone?toneDelta!=0:(left||right)&&samplerField<2;
        if(adjust&&samplerData) {
            int delta=samplerPage==SamplerPage::Tone?toneDelta:right?1:-1;auto& s=samplePatch.sound;
            unsigned field=samplerPage==SamplerPage::Trim?samplerField:samplerField+2;
            unsigned step=special.fn?1:std::max(1u,samplePatch.rate/100);
            if(field==0)s.start=std::max(0,std::min(int(s.end)-2,int(s.start)+delta*int(step)));
            if(field==1)s.end=std::max(int(s.start)+2,std::min(int(samplePatch.frames),int(s.end)+delta*int(step)));
            if(field<2)fitSampleLoop(s);
            if(field==2)s.root=std::max(24,std::min(96,int(s.root)+delta));
            if(field==3)s.mode=SampleMode((unsigned(s.mode)+(right?1:2))%3);
            if(field==4)s.attack=std::max(0,std::min(1000,int(s.attack)+delta*5));
            if(field==5)s.decay=std::max(0,std::min(2000,int(s.decay)+delta*10));
            if(field==6)s.sustain=std::max(0,std::min(100,int(s.sustain)+delta*5));
            if(field==7)s.release=std::max(5,std::min(2000,int(s.release)+delta*5));
            if(field==9)s.autoLevel=!s.autoLevel;
            if(field==8)s.gain=std::max(0,std::min(100,int(s.gain)+delta*5));
            if(field==10)s.cutoff=std::max(0,std::min(100,int(s.cutoff)+delta*2));
            if(field==11)s.resonance=std::max(0,std::min(90,int(s.resonance)+delta*5));
            if(field==12)s.drive=std::max(0,std::min(100,int(s.drive)+delta*5));
            if(field==13)s.bits=std::max(4,std::min(16,int(s.bits)+delta));
            if(field>=14&&field<=17){
                if(!s.loopRegion){s.loopStart=s.start;s.loopEnd=s.end;}
                if(field==14){s.loopRegion=!s.loopRegion;if(s.loopRegion)s.mode=SampleMode::Loop;}
                else if(field==15){s.loopRegion=true;s.mode=SampleMode::Loop;s.loopStart=std::max(int(s.start),std::min(int(s.loopEnd)-2,int(s.loopStart)+delta*int(step)));}
                else if(field==16){s.loopRegion=true;s.mode=SampleMode::Loop;s.loopEnd=std::max(int(s.loopStart)+2,std::min(int(s.end),int(s.loopEnd)+delta*int(step)));}
                else if(field==17)s.loopFade=std::max(0,std::min(int(std::min(2400u,(sampleLoopEnd(s)-sampleLoopStart(s))/4)),int(s.loopFade)+delta*int(special.fn?1:std::max(1u,samplePatch.rate/1000))));
            }
            if(field==18)s.pitchEnv=std::max(-48,std::min(48,int(s.pitchEnv)+delta));
            if(field==19)s.pitchDecay=std::max(0,std::min(2000,int(s.pitchDecay)+delta*(special.fn?1:10)));
            if(field==20)s.filterEnv=std::max(-100,std::min(100,int(s.filterEnv)+delta*(special.fn?1:5)));
            if(field==21)s.filterDecay=std::max(0,std::min(2000,int(s.filterDecay)+delta*(special.fn?1:10)));
            if(field==22)s.transpose=std::max(-24,std::min(24,int(s.transpose)+delta));
            if(field==23)s.fine=std::max(-100,std::min(100,int(s.fine)+delta));
            if(field==24){unsigned wave=s.oscA?s.oscA:samplePatch.baseSource;s.oscA=1+(int(wave)-1+delta+5)%5;}
            if(field==25)s.oscB=(int(s.oscB)+delta+6)%6;
            if(field==26)s.oscMix=std::max(0,std::min(100,int(s.oscMix)+delta*(special.fn?1:5)));
            if(field==27)s.oscTune=std::max(-24,std::min(24,int(s.oscTune)+delta));
            if(field==28)s.lfoTarget=(int(s.lfoTarget)+delta+4)%4;
            if(field==29)s.lfoShape=(int(s.lfoShape)+delta+4)%4;
            if(field==30)s.lfoRate=std::max(1,std::min(200,int(s.lfoRate)+delta*(special.fn?1:5)));
            if(field==31)s.lfoDepth=std::max(0,std::min(100,int(s.lfoDepth)+delta*(special.fn?1:5)));
            if(field<14||field>17)rebuildSampleToneWave(); // Loop markers do not alter the single-pass preview.
            samplerUnsaved=true;if(samplerPage==SamplerPage::Trim){sampleSilence();blockPiano(held);}else dirty=true;
        }
    }
    if(pressed('-')) {input.song.drums.volume=std::max(0,int(input.song.drums.volume)-10);dirty=true;}
    if(pressed('=')) {input.song.drums.volume=std::min(200,int(input.song.drums.volume)+10);dirty=true;}
    if(special.fn) {
        if(samplerPage==SamplerPage::Play) {
            if(left&&samplerOctave>2)--samplerOctave;
            if(right&&samplerOctave<6)++samplerOctave;
            if(left||right){sampleSilence();blockHeld(held);}
        }
        sampleControl.keys=0;return;
    }
    uint32_t keys=0;
    if(!backspace)for(unsigned key=32;key<127;++key)if(held[key]&&!blockedKeys[key]){int note=noteForKey(char(key));if(note>=0){keys|=1u<<note;if(!oldKeys[key])samplerLastMidi=(samplerOctave+1)*12+input.song.root+unsigned(note);}}
    if(keys!=sampleControl.keys)dirty=true;sampleControl.keys=keys;
}
void samplerText(int x,int y,const char* s,uint16_t color=ui::Text) {screen.setTextColor(color);screen.setCursor(x,y);screen.print(s);}
void sampleTabs() {
    const char* labels[]={"PLAY","TRIM","TONE","FILES"};bool basic=samplePatch.baseSource!=0;
    unsigned count=basic?3:4,width=240/count;
    for(unsigned col=0;col<count;++col){unsigned i=basic?(col?col+1:0):col;bool selected=unsigned(samplerPage)==i;
        screen.fillRect(col*width+2,17,width-3,15,selected?ui::Accent:ui::Panel);
        samplerText(col*width+8,21,labels[i],selected?ui::Background:ui::Muted);
        if(selected&&samplerEditing&&samplerPage==SamplerPage::Trim)screen.drawRect(col*width+2,17,width-3,15,ui::Yellow);
    }
}
void samplePixelWave(const int16_t* low,const int16_t* high,unsigned scale,int y,int height){
    int middle=y+height/2,half=height/2-1;scale=std::max(1u,scale);
    screen.drawFastHLine(4,middle,232,ui::Panel);
    for(unsigned x=0;x<116;++x){
        int top=middle-int(high[x])*half/int(scale),bottom=middle-int(low[x])*half/int(scale);
        screen.drawFastVLine(4+x*2,top,std::max(1,bottom-top+1),ui::Accent);
    }
}
void sampleWaveOverview(int y,int height){samplePixelWave(samplerMin,samplerMax,samplerWaveScale,y,height);}
void sampleEnvelopeCurve(int y,int height){
    if(!samplerToneWave.durationFrames)return;
    int previous=y+height-1;
    if(samplePatch.sound.mode!=SampleMode::OneShot)
        screen.drawFastVLine(4+int(samplerToneWave.envelopeHoldFraction(samplePatch.sound)*231),y,height,ui::Border);
    for(unsigned x=0;x<232;++x){
        float level=samplerToneWave.envelopeAt(x/231.f,samplePatch.sound);
        int point=y+height-1-int(level*(height-2));
        if(x)screen.drawLine(3+x,previous,4+x,point,ui::Yellow);previous=point;
    }
}
void sampleWave(int y,int height) {
    if(!samplerData||!samplePatch.frames){screen.drawFastHLine(4,y+height/2,232,ui::Panel);return;}
    sampleWaveOverview(y,height);
    screen.drawFastVLine(4+samplePatch.sound.start*231/samplePatch.frames,y,height,ui::Green);
    screen.drawFastVLine(4+(samplePatch.sound.end-1)*231/samplePatch.frames,y,height,ui::Yellow);
    if(samplerVoices.load())screen.drawFastVLine(4+std::min(samplerPosition.load(),samplePatch.frames-1)*231/samplePatch.frames,y,height,ui::Text);
}
void sampleCycleWave(int y,int height){
    const auto& wave=samplerToneWave.cycle;unsigned scale=std::max(16384u,wave.peak);
    samplePixelWave(wave.low,wave.high,scale,y,height);
    int mid=y+height/2,amp=std::max(1,height/2-1),previous=mid;
    for(unsigned x=0;x<116;++x){int point=mid-int(wave.high[x])*amp/int(scale);if(x)screen.drawLine(2+x*2,previous,4+x*2,point,ui::Accent);previous=point;}
}
void sampleTonePreview(int y,int height){
    if(!samplerData)return;
    const auto& sound=samplePatch.sound;
    bool cycle=samplerToneWave.hasCycle&&toneNav.group!=4;
    const char* guide=toneNav.group==7?"LFO TIME":toneNav.group==5?"PITCH TIME":toneNav.group==2?"FILTER TIME":"ENV TIME";
    screen.setTextColor(ui::Muted);screen.setCursor(5,y);screen.printf("%s / %s",cycle?(sound.oscB&&sound.oscMix&&sound.oscTune?"OSC VIEW":"1 CYCLE"):"CLIP",guide);
    y+=9;height-=9;if(height<8)return;
    if(!cycle&&sound.mode==SampleMode::Loop&&sound.loopRegion){
        int left=4+(sound.loopStart-sound.start)*231/(sound.end-sound.start),right=4+(sound.loopEnd-sound.start)*231/(sound.end-sound.start);
        screen.fillRect(left,y,std::max(1,right-left),height,ui::Selection);
    }
    if(cycle)sampleCycleWave(y,height);else samplePixelWave(samplerToneWave.low,samplerToneWave.high,32768,y,height);
    if(!cycle&&sound.mode==SampleMode::Loop&&sound.loopRegion){
        screen.drawFastVLine(4+(sound.loopStart-sound.start)*231/(sound.end-sound.start),y,height,ui::Green);
        screen.drawFastVLine(4+(sound.loopEnd-sound.start)*231/(sound.end-sound.start),y,height,ui::Yellow);
    }
    // Yellow guides use the complete note/sweep time axis; cyan cycle uses phase.
    int mid=y+height/2,amp=std::max(1,height/2-1),previousTop=mid,previousBottom=mid;
    for(unsigned x=0;x<232;++x){
        float level;
        bool sweep=toneNav.group==2||toneNav.group==5||toneNav.group==7;
        if(toneNav.group==7)level=sound.lfoTarget?sampleLfo(sound.lfoShape,x/232.f)*sound.lfoDepth/100.f:0;
        else if(sweep){unsigned decay=toneNav.group==5?sound.pitchDecay:sound.filterDecay;float amount=toneNav.group==5?sound.pitchEnv/48.f:sound.filterEnv/100.f;level=sampleSweep(unsigned(x*std::max(800u,decay)*24.f/231),decay)*amount;}
        else level=samplerToneWave.envelopeAt(x/231.f,sound);
        int top=mid-int(level*amp),bottom=mid+int(level*amp);
        if(x){screen.drawLine(3+x,previousTop,4+x,top,ui::Yellow);if(!sweep)screen.drawLine(3+x,previousBottom,4+x,bottom,ui::Yellow);}
        previousTop=top;previousBottom=bottom;
    }
}

void drawWorkspace() {
    if(workspace==Workspace::Player){drawPlayer();return;}
    screen.fillScreen(ui::Background);screen.setTextSize(1);
    screen.fillRect(0,0,3,16,ui::Accent);
    if(workspace==Workspace::Home) samplerText(7,3,"POCKET STUDIO 0.25.1");
    else if(samplerPage==SamplerPage::Start||samplerPage==SamplerPage::SessionLoad||samplerPage==SamplerPage::Bases)samplerText(7,3,samplerPage==SamplerPage::Start?"START SOUND LAB":samplerPage==SamplerPage::SessionLoad?"OPEN SESSION":"BASIC SOURCES");
    else if(samplerPage==SamplerPage::Presets || samplerPage==SamplerPage::Rename) samplerText(7,3,samplerSaving?"SAVE SOUND / SD":"LOAD SOUND / SD");
    else if(samplerPage==SamplerPage::Tone){
        screen.setTextColor(ui::Text);screen.setCursor(7,3);screen.printf("TONE / %.8s%s",samplerData?samplePatch.title:"EMPTY",samplerUnsaved?"*":"");samplerText(136,3,"^S SAVE",ui::Accent);
    }
    else if(samplerPage==SamplerPage::Play) {
        screen.setTextColor(ui::Text);screen.setCursor(7,3);screen.printf("LAB / %.15s",samplerData?samplePatch.title:"NO SAMPLE");
    } else samplerText(7,3,samplerPage==SamplerPage::Trim?"LAB / TRIM":samplerPage==SamplerPage::Tone?"LAB / TONE":samplerPage==SamplerPage::Browse?"OPEN WAV / SD":samplerPage==SamplerPage::Record?"LAB / RECORD":samplerPage==SamplerPage::TakeTrim?"TAKE / SELECT CLIP":samplerPage==SamplerPage::Process?"LAB / PROCESS":"LAB / FILES");
    screen.setTextColor(ui::Muted);screen.setCursor(193,3);if(battery>=0)screen.printf("BAT%u%%",unsigned(battery));else screen.print("BAT--%");
    if(workspace==Workspace::Home) {
        samplerText(7,24,"INSTRUMENTS",ui::Muted);
        for(unsigned i=0;i<3;++i) {
            int y=31+i*30;screen.fillRoundRect(6,y,228,28,3,homeSelection==i?ui::Selection:ui::Panel);
            screen.drawRoundRect(6,y,228,28,3,homeSelection==i?ui::Accent:ui::Border);
            samplerText(14,y+4,i==0?"STUDIO":i==1?"SOUND LAB":"PLAYER",homeSelection==i?ui::Yellow:ui::Text);
            samplerText(14,y+17,i==0?"S1-S4 / LOOPS / DRUM GRID":!sdOK?"SD CARD REQUIRED":i==1?"SD / WAV / TRIM / TONE":"SD / WAV + MP3 / STREAM",ui::Muted);
        }
        if(messageUntil&&samplerPage!=SamplerPage::Tone)samplerText(7,124,message,ui::Yellow);
        screen.pushSprite(0,0);return;
    }
    if(samplerPage==SamplerPage::Start){
        const char* titles[]={"OPEN SESSION","NEW SESSION","BASIC SOURCES"};
        const char* details[]={"LAST WORK / SAVED SOUNDS","EMPTY / RECORD OR IMPORT","SINE / TRI / SQUARE / SAW / NOISE"};
        for(unsigned i=0;i<3;++i){int y=24+i*34;bool selected=i==samplerStartChoice;
            screen.fillRoundRect(6,y,228,30,3,selected?ui::Selection:ui::Panel);screen.drawRoundRect(6,y,228,30,3,selected?ui::Accent:ui::Border);
            samplerText(14,y+5,titles[i],selected?ui::Yellow:ui::Text);samplerText(14,y+18,details[i],ui::Muted);
        }
        screen.pushSprite(0,0);return;
    }
    if(samplerPage==SamplerPage::SessionLoad||samplerPage==SamplerPage::Bases){
        bool bases=samplerPage==SamplerPage::Bases;unsigned choice=bases?samplerBaseChoice:samplerLoadChoice;
        const char* descriptions[]={"PURE","SOFT","HOLLOW","BRIGHT","TEXTURE"};
        for(unsigned i=0;i<5;++i){int y=23+i*21;bool selected=i==choice,available=bases||(i?samplerSlotValid[i-1]:samplerWorkAvailable);
            screen.fillRect(6,y,228,19,selected?ui::Selection:ui::Panel);
            if(bases){samplerText(14,y+6,sampleBaseName(i),selected?ui::Accent:ui::Text);samplerText(174,y+6,descriptions[i],ui::Muted);}
            else {samplerText(12,y+6,i?"SLOT":"LAST",selected?ui::Accent:ui::Muted);if(i){screen.setCursor(42,y+6);screen.printf("%u",i);}
                samplerText(60,y+6,available?(i?samplerSlotInfo[i-1].title:samplerWorkInfo.title):"EMPTY",available?(selected?ui::Accent:ui::Text):ui::Muted);}
        }
        if(messageUntil)samplerText(6,127,message,ui::Yellow);
        screen.pushSprite(0,0);return;
    }
    if(unsigned(samplerPage)<4)sampleTabs();
    if(!samplerData&&(samplerPage==SamplerPage::Play||samplerPage==SamplerPage::Trim||samplerPage==SamplerPage::Tone)){
        samplerText(16,53,"EMPTY SOUND LAB SESSION",ui::Text);samplerText(16,76,"OPEN WAV OR RECORD MIC",ui::Accent);
        if(messageUntil)samplerText(6,119,message,ui::Yellow);screen.pushSprite(0,0);return;
    }
    if(samplerPage==SamplerPage::Play) {
        if(samplePatch.baseSource&&samplerToneWave.hasCycle)sampleCycleWave(37,34);else sampleWave(37,34);
        if(samplerData) {
            unsigned base=(samplerOctave+1)*12+input.song.root,last=base+23;
            screen.setCursor(8,79);screen.setTextColor(ui::Text);
            screen.printf("%s%u-%s%u  %s  VOL%u",PitchNames[base%12],base/12-1,PitchNames[last%12],last/12-1,samplePatch.baseSource?(samplerToneWave.hasCycle?(samplePatch.sound.oscB&&samplePatch.sound.oscMix&&samplePatch.sound.oscTune?"OSC VIEW":"1 CYCLE"):"NOISE"):sampleModes[unsigned(samplePatch.sound.mode)],(input.song.drums.volume+1)/2);
            screen.setCursor(8,97);screen.setTextColor(ui::Muted);
            if(samplerLastMidi)screen.printf("LAST %s%u",PitchNames[samplerLastMidi%12],samplerLastMidi/12-1);else screen.print("LAST --");
            screen.setCursor(80,97);if(samplePatch.baseSource)screen.printf("TUNE %+dst",int(samplePatch.sound.transpose));else screen.printf("ROOT %s%u",PitchNames[samplePatch.sound.root%12],samplePatch.sound.root/12-1);
            samplerText(164,97,samplerUnsaved?"EDITED":"SAVED",samplerUnsaved?ui::Yellow:ui::Green);
            screen.setCursor(8,114);screen.setTextColor(ui::Muted);
            if(samplePatch.baseSource)screen.printf("A:%s B:%s GAIN%u%%",oscillatorName(samplePatch.sound.oscA?samplePatch.sound.oscA:samplePatch.baseSource),samplePatch.sound.oscB?oscillatorName(samplePatch.sound.oscB):"OFF",samplePatch.sound.gain);
            else screen.printf("%lu ms  GAIN%u%% VIEW AUTO",(unsigned long)((samplePatch.sound.end-samplePatch.sound.start)*1000/samplePatch.rate),samplePatch.sound.gain);
        }
    } else if(samplerPage==SamplerPage::Trim) {
        sampleWave(38,48);
        auto& s=samplePatch.sound;
        for(unsigned i=0;i<2;++i) {
            unsigned x=3+i*118;
            if(samplerField==i)screen.fillRect(x,90,116,22,ui::Panel);
            samplerText(x+5,97,i?"END":"START",samplerField==i?(i?ui::Yellow:ui::Green):ui::Muted);
            screen.setCursor(x+(i?29:41),97);screen.setTextColor(samplerField==i?(i?ui::Yellow:ui::Green):ui::Muted);
            if(samplePatch.rate)screen.printf("%.2fms",double(i?s.end:s.start)*1000/samplePatch.rate);else screen.print("--");
        }
        screen.fillRect(3,116,233,17,samplerField==2?ui::Selection:ui::Panel);
        screen.setCursor(8,121);screen.setTextColor(samplerField==2?ui::Accent:ui::Muted);
        screen.printf("SAVE TRIM  %lums",samplePatch.rate?(unsigned long)((s.end-s.start)*1000/samplePatch.rate):0ul);
    } else if(samplerPage==SamplerPage::Tone) {
        const char* groups[]={"SOUND","ENV","FILTER","FX","LOOP","PITCH","OSC","LFO"};
        const char* labels[]={"ROOT","MODE","ATTACK","DECAY","SUSTAIN","RELEASE","GAIN","AUTO","CUTOFF","RESO","DRIVE","BITS","REGION","IN","OUT","XFADE","SWEEP","TIME","SWEEP","TIME","TUNE","FINE","A WAVE","B WAVE","MIX B","B TUNE","TARGET","SHAPE","RATE","DEPTH"};
        const auto& s=samplePatch.sound;
        unsigned visible[8]{},total=0,selectedIndex=0;
        for(unsigned g=0;g<8;++g)if(toneNav.enabled(g)){if(g==toneNav.group)selectedIndex=total;visible[total++]=g;}
        unsigned first=selectedIndex>2?std::min(selectedIndex-2,total-5):0;
        for(unsigned col=0;col<5&&first+col<total;++col){unsigned g=visible[first+col],gx=col*48;bool selected=g==toneNav.group,focus=selected&&samplerEditing&&toneNav.groups;
            screen.fillRect(1+gx,35,47,13,focus?ui::Accent:selected?ui::Selection:ui::Background);
            samplerText(1+gx+(47-std::strlen(groups[g])*6)/2,38,groups[g],focus?ui::Background:selected?ui::Text:ui::Muted);
        }
        if(messageUntil){samplerText(6,50,message,ui::Yellow);sampleTonePreview(61,43);}else sampleTonePreview(50,54);
        for(unsigned row=0;row<toneNav.rows();++row){
            unsigned field=toneNav.parameterAt(row);int y=107;
            int width=240/toneNav.rows(),x=row*width;
            bool selected=samplerEditing&&!toneNav.groups&&row==toneNav.row;
            screen.fillRect(x+1,y,width-2,27,selected?ui::Selection:ui::Panel);
            char value[24]{};unsigned meter=0;
            if(field==0){snprintf(value,sizeof value,"%s%u",PitchNames[s.root%12],s.root/12-1);meter=(s.root-24)*100/72;}
            if(field==1){snprintf(value,sizeof value,"%s",sampleModes[unsigned(s.mode)]);meter=unsigned(s.mode)*50;}
            if(field==2){snprintf(value,sizeof value,"%ums",s.attack);meter=s.attack/10;}
            if(field==3){snprintf(value,sizeof value,"%ums",s.decay);meter=s.decay/20;}
            if(field==4){snprintf(value,sizeof value,"%u%%",s.sustain);meter=s.sustain;}
            if(field==5){snprintf(value,sizeof value,"%ums",s.release);meter=s.release/20;}
            if(field==6){snprintf(value,sizeof value,"%u%%",s.gain);meter=s.gain;}
            if(field==7){snprintf(value,sizeof value,"%s",s.autoLevel?"ON":"OFF");meter=s.autoLevel?100:0;}
            if(field==8){if(s.cutoff==100)snprintf(value,sizeof value,"OFF");else snprintf(value,sizeof value,"%uHz",unsigned(SampleEffects::cutoffHz(s.cutoff)));meter=s.cutoff;}
            if(field==9){snprintf(value,sizeof value,"%u%%",s.resonance);meter=s.resonance*100/90;}
            if(field==10){snprintf(value,sizeof value,"%u%%",s.drive);meter=s.drive;}
            if(field==11){snprintf(value,sizeof value,"%ubit",s.bits);meter=(s.bits-4)*100/12;}
            if(field==12){snprintf(value,sizeof value,"%s",s.loopRegion?"CUSTOM":"FULL");meter=s.loopRegion?100:0;}
            if(field==13||field==14){unsigned position=field==13?sampleLoopStart(s):sampleLoopEnd(s);snprintf(value,sizeof value,"%.0fms",position*1000.f/samplePatch.rate);meter=(position-s.start)*100/(s.end-s.start);}
            if(field==15){snprintf(value,sizeof value,"%.0fms",sampleLoopFade(s)*1000.f/samplePatch.rate);meter=sampleLoopFade(s)*100/std::max(1u,std::min(2400u,(sampleLoopEnd(s)-sampleLoopStart(s))/4));}
            if(field==16){snprintf(value,sizeof value,"%+dst",int(s.pitchEnv));meter=(s.pitchEnv+48)*100/96;}
            if(field==17){snprintf(value,sizeof value,"%ums",s.pitchDecay);meter=s.pitchDecay/20;}
            if(field==18){snprintf(value,sizeof value,"%+d%%",int(s.filterEnv));meter=(s.filterEnv+100)/2;}
            if(field==19){snprintf(value,sizeof value,"%ums",s.filterDecay);meter=s.filterDecay/20;}
            if(field==20){snprintf(value,sizeof value,"%+dst",int(s.transpose));meter=(s.transpose+24)*100/48;}
            if(field==21){snprintf(value,sizeof value,"%+dct",int(s.fine));meter=(s.fine+100)/2;}
            if(field==22){snprintf(value,sizeof value,"%s",oscillatorName(s.oscA?s.oscA:samplePatch.baseSource));meter=(s.oscA?s.oscA:samplePatch.baseSource)*20;}
            if(field==23){snprintf(value,sizeof value,"%s",s.oscB?oscillatorName(s.oscB):"OFF");meter=s.oscB*20;}
            if(field==24){snprintf(value,sizeof value,"%u%%",s.oscMix);meter=s.oscMix;}
            if(field==25){snprintf(value,sizeof value,"%+dst",int(s.oscTune));meter=(s.oscTune+24)*100/48;}
            if(field==26){snprintf(value,sizeof value,"%s",lfoTargetName(s.lfoTarget));meter=s.lfoTarget*100/3;}
            if(field==27){snprintf(value,sizeof value,"%s",lfoShapeName(s.lfoShape));meter=s.lfoShape*100/3;}
            if(field==28){snprintf(value,sizeof value,"%u.%uHz",s.lfoRate/10,s.lfoRate%10);meter=s.lfoRate/2;}
            if(field==29){snprintf(value,sizeof value,"%u%%",s.lfoDepth);meter=s.lfoDepth;}
            bool inactive=((field==24||field==25)&&!s.oscB)||(field>=27&&field<=29&&!s.lfoTarget)||(field==9&&s.cutoff==100&&!s.filterEnv&&!(s.lfoTarget==2&&s.lfoDepth))||(field==17&&!s.pitchEnv)||(field==19&&!s.filterEnv)||(field==3&&s.sustain==100)||(field==5&&s.mode==SampleMode::OneShot)||(field>=12&&field<=15&&s.mode!=SampleMode::Loop);uint16_t color=inactive?ui::Muted:selected?ui::Accent:ui::Text;
            screen.fillRect(x+4,y+25,(width-8)*std::min(100u,meter)/100,1,selected?ui::Accent:ui::Border);
            samplerText(x+4,y+2,labels[field],color);samplerText(x+4,y+14,value,color);
        }
    } else if(samplerPage==SamplerPage::Record) {
        bool recording=micTake==MicTake::Capture,countdown=micTake==MicTake::Countdown;
        samplerText(8,24,recording?"RECORDING":countdown?"GET READY":"MICROPHONE TAKE",recording?ui::Danger:ui::Yellow);
        samplerText(8,42,"24kHz / MONO / PCM16",ui::Muted);
        screen.setCursor(8,57);screen.setTextColor(ui::Text);
        if(recording)screen.printf("TIME %.2fs",micCompleted.load()/24000.f);
        else if(countdown)screen.printf("STARTS IN %us",micCountdownShown);
        else screen.printf("DELAY %us   < / > SET",micDelaySeconds);
        bool card=recording?micToSD:sdOK;
        samplerText(8,73,card?"SD: UNTIL YOU STOP":"SD REQUIRED",card?ui::Green:ui::Yellow);
        screen.fillRect(8,89,224,18,ui::Selection);
        samplerText(25,94,recording?"ENTER  STOP AND KEEP":countdown?"ENTER  CANCEL COUNTDOWN":"ENTER  START RECORDING",ui::Accent);
        screen.fillRect(8,119,224,8,ui::Panel);screen.fillRect(8,119,std::min(224u,micPeak*224/32768),8,micPeak>30000?ui::Danger:ui::Green);
    } else if(samplerPage==SamplerPage::TakeTrim) {
        screen.setCursor(8,22);screen.setTextColor(ui::Muted);screen.printf("%.22s",std::strrchr(recordedTakePath,'/')?std::strrchr(recordedTakePath,'/')+1:recordedTakePath);
        if(takeWaveError)samplerText(170,22,"WAVE ERR",ui::Yellow);
        else if(takeWave.done<takeWave.frames){screen.setCursor(182,22);screen.printf("%u%%",unsigned(uint64_t(takeWave.done)*100/std::max(1u,takeWave.frames)));}
        unsigned total=std::max(1u,unsigned(recordedTake.frames));int first=4+unsigned(uint64_t(takeStart)*231/total),last=4+unsigned(uint64_t(takeEnd)*231/total);
        screen.fillRect(first,35,std::max(1,last-first),39,ui::Selection);
        samplePixelWave(takeWave.low,takeWave.high,std::max(64u,takeWave.peak),35,39);
        screen.drawFastVLine(first,35,39,ui::Green);screen.drawFastVLine(last,35,39,ui::Yellow);
        if(takePreview){unsigned position=takePreview->restartAt?takeStart:takePreview->start+takePreview->stream.position.load();screen.drawFastVLine(4+unsigned(uint64_t(std::min(position,total))*231/total),35,39,ui::Text);}
        const char* labels[]={"START","END","MOVE"};
        for(unsigned i=0;i<3;++i){int x=3+i*79;bool selected=takeField==i;screen.fillRect(x,79,76,25,selected?ui::Selection:ui::Panel);
            samplerText(x+4,82,labels[i],selected?ui::Accent:ui::Muted);screen.setCursor(x+4,94);screen.setTextColor(ui::Text);screen.printf("%.3fs",double(i==1?takeEnd:takeStart)/std::max(1u,unsigned(recordedTake.rate)));}
        screen.setCursor(6,109);screen.setTextColor(ui::Muted);screen.printf("CLIP %.2fs  STEP %.3fs",double(takeEnd-takeStart)/std::max(1u,unsigned(recordedTake.rate)),double(takeLastStep)/std::max(1u,unsigned(recordedTake.rate)));
        screen.fillRect(3,120,115,14,takePreview?ui::Selection:ui::Panel);screen.fillRect(121,120,115,14,ui::Panel);
        samplerText(8,123,takePreview?"SPACE STOP":"SPACE PLAY",takePreview?ui::Yellow:ui::Accent);
        samplerText(129,123,takeEnd-takeStart>32768?"TOO LONG":"ENTER LOAD",takeEnd-takeStart>32768?ui::Yellow:ui::Green);
    } else if(samplerPage==SamplerPage::Process) {
        const char* labels[]={"NORMALIZE -0.9dB","REVERSE","REMOVE DC","FADE EDGES 5ms","UNDO / REDO AUDIO"};
        for(unsigned i=0;i<5;++i){int y=34+i*18;if(i==sampleProcess)screen.fillRect(3,y-3,233,16,ui::Selection);samplerText(8,y,labels[i],i==sampleProcess?ui::Accent:ui::Text);}
        samplerText(8,126,"Edits selected START-END region",ui::Muted);
    } else if(samplerPage==SamplerPage::Files || samplerPage==SamplerPage::Actions) {
        bool files=samplerPage==SamplerPage::Files;
        const char* labels[]={"OPEN WAV","SAVE SOUND","LOAD SOUND","PLAY","TRIM","TONE","RECORD MIC","EXPORT WAV","PROCESS AUDIO","INSTRUMENTS / HOME","CHANGE BASE SOURCE"};
        if(!files)samplerText(4,20,"ACTIONS",ui::Yellow);
        unsigned fileCount=samplerFileCount(samplePatch.baseSource!=0);
        unsigned first=files?0:samplerAction/6*6;
        for(unsigned row=0;row<(files?fileCount:6u);++row) {
            unsigned i=first+row;if(i>=(files?fileCount:10u))break;
            unsigned label=files?samplerFileActionAt(i,samplePatch.baseSource!=0):i;
            int y=38+row*(files?12:14);
            bool selected=i==(files?samplerFileAction:samplerAction);
            if(selected)screen.fillRect(3,y-2,233,files?12:14,ui::Selection);
            samplerText(8,y,selected?">":" ",ui::Accent);samplerText(23,y,labels[label],selected?ui::Accent:ui::Text);
        }
        if(files) {
            samplerText(8,126,sdOK?"SD READY / 4 SD SOUNDS":"SD REQUIRED",sdOK?ui::Green:ui::Yellow);
        }
    } else if(samplerPage==SamplerPage::Browse) {
        samplerText(4,20,"OPEN WAV / SD",ui::Yellow);
        unsigned first=samplerFile/6*6;
        for(unsigned row=0;row<6 && first+row<samplerFiles;++row) {
            unsigned item=first+row;int y=37+row*15;
            if(item==samplerFile)screen.fillRect(2,y-2,235,14,ui::Selection);
            char line[39];snprintf(line,sizeof line,"%c %.34s",item==samplerFile?'>':' ',samplerFileNames[item]);
            samplerText(4,y,line,item==samplerFile?ui::Accent:ui::Text);
        }
        screen.setCursor(4,126);screen.setTextColor(ui::Muted);screen.printf("%u/%u  PCM16 8-48k",samplerFiles?samplerFile+1:0,samplerFiles);
    } else if(samplerPage==SamplerPage::Presets || samplerPage==SamplerPage::Rename) {
        screen.setCursor(7,21);screen.setTextColor(samplerUnsaved?ui::Yellow:ui::Muted);
        screen.printf("%.15s  %s",samplerSaving?samplerName:samplePatch.title,samplerUnsaved?"* edited":"saved");
        for(unsigned slot=0;slot<4;++slot) {
            int y=39+slot*19;if(slot==samplerSlot)screen.fillRect(3,y-3,234,18,ui::Panel);
            samplerText(8,y,slot==samplerSlot?">":" ",ui::Accent);
            screen.setCursor(22,y);screen.setTextColor(ui::Muted);screen.printf("%u",slot+1);
            samplerText(42,y,samplerSlotValid[slot]?samplerSlotInfo[slot].title:samplerSlotPresent[slot]?"INVALID":"EMPTY",samplerSlotValid[slot]?ui::Text:ui::Muted);
        }
        if(samplerSaving){screen.setCursor(4,121);screen.setTextColor(ui::Muted);screen.printf("NAME: %.15s",samplerName);}
        if(samplerPage==SamplerPage::Rename || samplerConfirm) {
            screen.fillRect(7,43,226,66,ui::Panel);screen.drawRect(7,43,226,66,ui::Yellow);
            samplerText(14,50,samplerConfirm?"Replace this sound?":"NAME / 15 characters",ui::Yellow);
            samplerText(14,68,samplerName);
            if(samplerConfirm){screen.setCursor(14,83);screen.printf("SD SLOT %u",samplerSlot+1);}
            samplerText(14,97,"ENTER confirm / TAB cancel",ui::Muted);
        }
    } else if(samplerPage==SamplerPage::Result) {
        samplerText(8,26,samplerResultOK?"SOUND READY":"ACTION FAILED",samplerResultOK?ui::Green:ui::Danger);
        char first[37];snprintf(first,sizeof first,"%.36s",samplerResult);samplerText(8,49,first,samplerResultOK?ui::Green:ui::Yellow);
        if(std::strlen(samplerResult)>36)samplerText(8,61,samplerResult+36,ui::Yellow);
        samplerText(8,71,samplePatch.title);
        samplerText(8,101,samplerResultOK?"ENTER return to playing":"ENTER return",ui::Muted);
    }
    screen.pushSprite(0,0);
}




