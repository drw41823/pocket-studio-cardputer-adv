#include "PlayerTestMP3.h"
__attribute__((noinline)) bool consumePlayerCheck(){
    int16_t out[128];unsigned peak=0,blocks=0;bool paused=false;
    for(;blocks<1800;++blocks){
        tickPlayer();auto& p=*player;
        if(!paused&&p.stream.position.load()>2000){
            p.stream.playing=false;unsigned at=p.stream.position.load();renderPlayer(out,128);if(p.stream.position.load()!=at)return false;p.stream.playing=true;paused=true;
        }
        renderPlayer(out,128);peak=std::max(peak,samplePeak(out,128));
        if(p.producerEnd&&!p.stream.playing&&!p.stream.size())break;
    }
    Serial.printf("PLAYER_CHECK format=%s rate=%u frames=%u peak=%u underruns=%u decode_max_us=%u blocks=%u\n",player->mp3?"MP3":"WAV",player->rate,player->stream.position.load(),peak,player->stream.underruns.load(),unsigned(player->worstDecodeUs),blocks);
    return blocks<1800&&peak>100&&paused&&player->stream.underruns.load()==0;
}
__attribute__((noinline)) bool realtimePlayerCheck(const char* path){
    if(!openPlayerFile(path)||!pauseForFlash())return false;
    playerCheckMuted=true;workspace=Workspace::Player;publish();resumeAudio();
    uint32_t begin=millis(),drawAt=0;bool done=false;
    while(millis()-begin<5000){
        tickPlayer();if(millis()-drawAt>250){drawAt=millis();drawPlayer();}
        if(player->producerEnd&&!player->stream.playing&&!player->stream.size()){done=true;break;}delay(2);
    }
    Serial.printf("PLAYER_REALTIME format=%s wall_ms=%u frames=%u underruns=%u\n",player->mp3?"MP3":"WAV",unsigned(millis()-begin),player->stream.position.load(),player->stream.underruns.load());
    unsigned expected=player->rate?unsigned(uint64_t(player->stream.position.load())*1000/player->rate):0;
    unsigned wall=millis()-begin;
    bool ok=done&&expected&&player->stream.underruns.load()==0&&wall>=expected*8/10&&wall<=expected*13/10+100;
    if(!pauseForFlash())return false;workspace=Workspace::Home;publish();resumeAudio();playerCheckMuted=false;return ok;
}
__attribute__((noinline)) bool checkPlayerBrowser(const char* root){
    char child[96],file[128];snprintf(child,sizeof child,"%s/NESTED",root);snprintf(file,sizeof file,"%s/ENTRY.WAV",child);
    if(!SD.mkdir(child))return false;
    File f=SD.open(file,FILE_WRITE);bool ok=bool(f);f.close();
    snprintf(player->folder,sizeof player->folder,"%s",root);player->offset=player->selected=0;scanPlayerFiles();
    bool found=false;for(unsigned i=0;i<player->count;++i)if(player->directories[i]&&!std::strcmp(player->paths[i],child)){player->selected=i;found=true;break;}
    ok=ok&&found&&player->enter();if(ok){scanPlayerFiles();ok=player->count==1&&!player->directories[0]&&!std::strcmp(player->paths[0],file)&&player->parent()&&!std::strcmp(player->folder,root);}
    if(!SD.remove(file))ok=false;if(!SD.rmdir(child))ok=false;
    Serial.printf("PLAYER_BROWSER %s nested enter parent audio filter\n",ok?"PASS":"FAIL");return ok;
}
__attribute__((noinline)) void runPlayerCheck(){
    if(workspace!=Workspace::Home||player||samplerData||!ensureSamplerSD()){Serial.println("PLAYER_CHECK SKIP: use Home after boot with SD");return;}
    char dir[48],wav[96],mp3[96];bool created=false;
    for(unsigned i=1;i<1000;++i){snprintf(dir,sizeof dir,"/PocketSynth/PLAYER_QA%04u",i);if(!SD.exists(dir)){created=SD.mkdir(dir);break;}}
    if(!created){Serial.println("PLAYER_CHECK FAIL: test directory");return;}
    snprintf(wav,sizeof wav,"%s/SOURCE.wav",dir);snprintf(mp3,sizeof mp3,"%s/SOURCE.mp3",dir);
    playerCheckMode=true;bool ok=[&](){
        if(!writeCheckFixture(wav))return false;
        File f=SD.open(mp3,FILE_WRITE);bool wrote=f&&f.write(PlayerTestMP3,sizeof PlayerTestMP3)==sizeof PlayerTestMP3;f.flush();f.close();if(!wrote)return false;
        if(!pauseForFlash())return false;dropSynthSampleCache();player=new(std::nothrow)PlayerState;resumeAudio();if(!player)return false;
        if(!checkPlayerBrowser(dir))return false;
        // Keep shared workspace Home: the hardware audio task outputs silence,
        // while the UI consumes production decoder/ring output into a local buffer.
        if(!openPlayerFile(wav)||!consumePlayerCheck())return false;
        if(!openPlayerFile(mp3)||!consumePlayerCheck())return false;
        playerSeek(-100);player->prebuffer=true;if(!consumePlayerCheck())return false;
        if(!realtimePlayerCheck(wav)||!realtimePlayerCheck(mp3))return false;
        return true;
    }();
    closePlayer();playerCheckMode=false;playerCheckMuted=false;
    if(SD.exists(wav)&&!SD.remove(wav))ok=false;if(SD.exists(mp3)&&!SD.remove(mp3))ok=false;if(!SD.rmdir(dir))ok=false;
    Serial.printf("PLAYER_CHECK %s WAV MP3 pause seek EOF cleanup\n",ok?"PASS":"FAIL");dirty=true;
}
