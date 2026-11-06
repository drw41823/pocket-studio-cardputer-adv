// UI task owns SD and decoding. The audio task only consumes bounded PCM.
struct PlayerState : PlayerBrowser {
    AudioStream stream;File file;WavInfo wav;MP3Decoder* decoder=nullptr;
    uint8_t compressed[4096]{};int16_t decoded[2304]{};
    char current[128]{};
    unsigned filled=0,sourceAt=0,rate=0,bitrate=0,dataStart=0,dataEnd=0,totalFrames=0;
    bool mp3=false,listing=true,opened=false,producerEnd=false,approximate=false,prebuffer=false;
    uint32_t worstDecodeUs=0,lastDraw=0;
    ~PlayerState(){file.close();destroyMP3(decoder);}
};
PlayerState* player=nullptr;
bool playerCheckMode=false;
std::atomic<bool> playerCheckMuted{false};
bool playerExtension(const char* p,const char* ext){return PlayerBrowser::extension(p,ext);}
void tickPlayer();
void scanPlayerFiles(){
    if(!player||!ensureSamplerSD())return;auto& p=*player;
    File directory=SD.open(p.folder),entry;
    if(!directory||!directory.isDirectory()){directory.close();p.count=0;p.hasNext=false;tell("SD directory unavailable");dirty=true;return;}
    p.readPage([&](const char*& name,bool& isDirectory){entry.close();entry=directory.openNextFile();if(!entry)return false;name=entry.name();isDirectory=entry.isDirectory();return true;},[](){tickPlayer();});
    entry.close();directory.close();dirty=true;
}
bool nextPlayerTrack(int direction){
    if(!player)return false;auto& p=*player;
    while(true){auto moved=p.move(direction);if(moved==PlayerBrowser::Move::None)return false;if(moved==PlayerBrowser::Move::Page)scanPlayerFiles();if(p.selected<p.count&&!p.directories[p.selected])return true;}
}
void renderPlayer(int16_t* out,unsigned n){if(player)player->stream.render(out,n);else std::fill(out,out+n,0);if(playerCheckMuted.load())std::fill(out,out+n,0);}
bool enterPlayer(){
    if(!ensureSamplerSD())return false;
    if(!pauseForFlash())return false;dropSynthSampleCache();
    if(!player&&ESP.getFreeHeap()>sizeof(PlayerState)+32768)player=new(std::nothrow)PlayerState;
    if(!player){resumeAudio();tell("Player memory allocation failed");return false;}
    workspace=Workspace::Player;publish();resumeAudio();scanPlayerFiles();dirty=true;return true;
}
void closePlayer(){
    if(!player)return;if(!pauseForFlash())return;
    player->stream.playing=false;workspace=Workspace::Home;publish();auto* old=player;player=nullptr;delete old;resumeAudio();dirty=true;
}
__attribute__((noinline,noclone)) bool openPlayerFile(const char* path){
    if(!player||!ensureSamplerSD()||!pauseForFlash())return false;auto& p=*player;
    p.stream.playing=false;p.file.close();destroyMP3(p.decoder);p.decoder=nullptr;p.opened=false;p.producerEnd=false;p.filled=p.sourceAt=p.rate=p.bitrate=p.dataStart=p.totalFrames=0;
    p.mp3=playerExtension(path,".mp3");p.approximate=p.mp3;p.stream.reset(48000);p.prebuffer=true;
    p.file=SD.open(path,FILE_READ);bool ok=p.file&&uint64_t(p.file.size())<=0xffffffffu;p.dataEnd=ok?unsigned(p.file.size()):0;
    if(ok&&!p.mp3){ok=inspectWav(sampleReadAt,&p.file,p.dataEnd,p.wav,0xffffffffu)==WavError::None;if(ok){p.rate=p.wav.rate;p.totalFrames=p.wav.frames;p.dataStart=p.wav.offset;p.stream.rate=p.rate;}}
    if(ok&&p.mp3){
        uint8_t id3[10];if(p.file.size()>=10&&p.file.read(id3,10)==10&&!std::memcmp(id3,"ID3",3)){
            if((id3[6]|id3[7]|id3[8]|id3[9])&0x80)ok=false;
            else p.dataStart=10+(unsigned(id3[6])<<21)+(unsigned(id3[7])<<14)+(unsigned(id3[8])<<7)+id3[9]+((id3[5]&16)?10:0);
        }
        if(p.dataStart>=p.dataEnd)ok=false;
        if(ok){p.decoder=createMP3();ok=p.decoder&&p.file.seek(p.dataStart);}
    }
    if(!ok){p.file.close();destroyMP3(p.decoder);p.decoder=nullptr;p.prebuffer=false;resumeAudio();tell("Use PCM16 WAV or valid MP3");return false;}
    snprintf(p.current,sizeof p.current,"%s",path);p.opened=true;p.listing=false;resumeAudio();dirty=true;return true;
}
void playerSeek(int seconds){
    if(!player||!player->opened||!player->rate)return;auto& p=*player;
    unsigned now=p.stream.position.load()/p.rate;int target=std::max(0,int(now)+seconds);
    if(p.totalFrames)target=std::min(target,int(p.totalFrames/p.rate));
    bool playing=p.stream.playing.load()||p.prebuffer;if(!pauseForFlash())return;
    p.stream.reset(p.rate,unsigned(target)*p.rate);p.producerEnd=false;p.filled=0;p.prebuffer=playing;
    if(p.mp3){uint64_t byte=uint64_t(p.dataStart)+uint64_t(target)*p.bitrate*125;byte=std::min(byte,uint64_t(p.dataEnd));p.file.seek(unsigned(byte));resetMP3(p.decoder);}
    else p.sourceAt=std::min(p.wav.frames,unsigned(target)*p.rate);
    resumeAudio();dirty=true;
}
__attribute__((noinline)) void tickPlayer(){
    if(!player||(workspace!=Workspace::Player&&!playerCheckMode))return;auto& p=*player;
    if(!p.opened)return;
    if(!p.producerEnd){
        for(unsigned batch=0;batch<3&&p.stream.free()>=1152;++batch){
            uint32_t begin=micros();unsigned count=0;
            if(!p.mp3){
                count=std::min(1024u,p.wav.frames-p.sourceAt);WavInfo part=p.wav;part.frames=count;part.offset=p.wav.offset+p.sourceAt*p.wav.channels*2;
                if(!count)p.producerEnd=true;
                else if(!decodeWav(sampleReadAt,&p.file,part,p.decoded,2304)){p.producerEnd=true;tell("SD read failed");count=0;}
                else p.sourceAt+=count;
            }else{
                if(p.filled<sizeof p.compressed&&p.file.position()<p.dataEnd){unsigned n=std::min(unsigned(sizeof p.compressed)-p.filled,p.dataEnd-unsigned(p.file.position()));int got=p.file.read(p.compressed+p.filled,n);if(got>0)p.filled+=unsigned(got);else{p.producerEnd=true;tell("MP3 SD read failed");}}
                MP3Frame info;int decoded=decodeMP3(p.decoder,p.compressed,p.filled,p.decoded,info);
                if(info.bytes>0&&unsigned(info.bytes)<=p.filled){p.filled-=info.bytes;std::memmove(p.compressed,p.compressed+info.bytes,p.filled);}
                else if(p.file.position()>=p.dataEnd){p.producerEnd=true;}
                else if(p.filled==sizeof p.compressed){p.filled-=512;std::memmove(p.compressed,p.compressed+512,p.filled);}
                if(decoded>0){
                    if((info.channels!=1&&info.channels!=2)||info.rate<8000||info.rate>48000||(p.rate&&p.rate!=unsigned(info.rate))){p.producerEnd=true;tell("Unsupported MP3 rate change");}
                    else{
                        if(!p.rate){p.rate=info.rate;p.bitrate=std::max(1,info.bitrate);p.stream.rate=p.rate;p.totalFrames=unsigned(std::min(uint64_t(0xffffffffu),uint64_t(p.dataEnd-p.dataStart)*p.rate/(p.bitrate*125)));}
                        count=unsigned(decoded);if(info.channels==2)for(unsigned i=0;i<count;++i)p.decoded[i]=int16_t((int(p.decoded[i*2])+int(p.decoded[i*2+1]))/2);
                    }
                }
            }
            p.worstDecodeUs=std::max(p.worstDecodeUs,uint32_t(micros()-begin));
            if(count&&!p.stream.push(p.decoded,count)){p.producerEnd=true;tell("Player buffer error");}
            if(p.producerEnd){p.stream.eof=true;break;}
        }
    }
    if(p.prebuffer&&(p.stream.size()>=4096||p.producerEnd)){p.prebuffer=false;p.stream.playing=p.stream.size()>0;dirty=true;}
    if(p.stream.finished.exchange(false)){dirty=true;}
    if(millis()-p.lastDraw>=250){p.lastDraw=millis();dirty=true;}
}
__attribute__((noinline)) void scanPlayer(const uint8_t* held,bool enter,bool tab,bool backspace){
    if(!player)return;auto& p=*player;auto pressed=[&](char c){return held[unsigned(c)]&&!oldKeys[unsigned(c)];};
    bool up=sampleNav(held,';',0),down=sampleNav(held,'.',1),left=sampleNav(held,',',2),right=sampleNav(held,'/',3);
    if(pressed('-')||pressed('=')){unsigned v=p.stream.volume.load();p.stream.volume=unsigned(std::max(0,std::min(100,int(v)+(pressed('=')?5:-5))));dirty=true;}
    if(pressed('r')&&p.listing){p.offset=p.selected=0;scanPlayerFiles();}
    if(tab&&p.opened){p.listing=!p.listing;dirty=true;}
    if(backspace&&p.listing){if(p.parent())scanPlayerFiles();}
    else if(backspace){p.stream.playing=false;p.prebuffer=false;playerSeek(-int(p.stream.position.load()/std::max(1u,p.rate)));p.listing=true;dirty=true;}
    else if(p.listing){
        if(up||down){auto moved=p.move(up?-1:1);if(moved==PlayerBrowser::Move::Page)scanPlayerFiles();dirty=true;}
        if(enter&&p.count){if(p.enter())scanPlayerFiles();else openPlayerFile(p.paths[p.selected]);}
    }else{
        if(enter||pressed(' ')){if(p.stream.eof.load()&&!p.stream.size()){playerSeek(-int(p.stream.position.load()/std::max(1u,p.rate)));p.prebuffer=true;}else{p.stream.playing=!p.stream.playing.load();p.prebuffer=false;}dirty=true;}
        if(left||right)playerSeek(left?-5:5);
        if((up||down)&&nextPlayerTrack(up?-1:1))openPlayerFile(p.paths[p.selected]);
    }
    if(enter||tab||backspace)blockHeld(held);
}
__attribute__((noinline)) void drawPlayer(){
    screen.fillScreen(ui::Background);screen.setTextSize(1);samplerText(7,3,"AUDIO PLAYER",ui::Accent);
    screen.setCursor(193,3);screen.setTextColor(ui::Muted);if(battery>=0)screen.printf("BAT%u%%",unsigned(battery));
    if(!player){screen.pushSprite(0,0);return;}auto& p=*player;
    if(p.listing){
        screen.setCursor(7,21);screen.setTextColor(ui::Yellow);screen.printf("SD %.34s",p.folder);
        if(!p.count)samplerText(7,47,"No folders or WAV / MP3",ui::Muted);
        unsigned first=p.selected/6*6;
        for(unsigned row=0;row<6&&first+row<p.count;++row){unsigned item=first+row;int y=38+row*14;if(item==p.selected)screen.fillRect(3,y-2,233,13,ui::Selection);const char* name=std::strrchr(p.paths[item],'/');screen.setCursor(8,y);screen.setTextColor(item==p.selected?ui::Accent:ui::Text);screen.printf("%s%.*s",p.directories[item]?"[DIR] ":"",p.directories[item]?30:36,name?name+1:p.paths[item]);}
        screen.setCursor(7,124);screen.setTextColor(ui::Muted);screen.printf("%u-%u%s VOL%u%%%s",p.count?p.offset+1:0,p.offset+p.count,p.hasNext?" +":"",p.stream.volume.load(),p.omitted?" LONG PATH":"");
    }else{
        const char* name=std::strrchr(p.current,'/');screen.setCursor(7,25);screen.setTextColor(ui::Text);screen.printf("%.36s",name?name+1:p.current);
        samplerText(7,45,p.prebuffer?"BUFFERING":p.stream.playing.load()?"PLAYING":"PAUSED",p.stream.playing.load()?ui::Green:ui::Yellow);
        screen.setCursor(142,45);screen.setTextColor(ui::Muted);screen.printf("%s %uk",p.mp3?"MP3":"WAV",p.rate/1000);
        unsigned at=p.stream.position.load(),seconds=p.rate?at/p.rate:0,total=p.rate?p.totalFrames/p.rate:0;
        screen.fillRect(7,67,226,10,ui::Panel);if(p.totalFrames)screen.fillRect(7,67,std::min(226u,unsigned(uint64_t(at)*226/p.totalFrames)),10,ui::Accent);
        screen.setCursor(7,85);screen.setTextColor(ui::Text);screen.printf("%02u:%02u / %s%02u:%02u",seconds/60,seconds%60,p.approximate?"~":"",total/60,total%60);
        screen.setCursor(7,107);screen.printf("VOLUME %u%%",p.stream.volume.load());
    }
    if(messageUntil)samplerText(7,124,message,ui::Yellow);screen.pushSprite(0,0);
}
