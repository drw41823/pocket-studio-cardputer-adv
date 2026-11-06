#include "SamplerStorage.h"
#include "AudioPauseGate.h"
#include <map>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace pocket;
void require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct MemoryFS {
    std::map<std::string,std::vector<uint8_t>> files;std::string failRename;bool shortWrite=false,badRead=false;
    bool exists(const char* p){return files.count(p);}
    bool remove(const char* p){return files.erase(p);}
    bool rename(const char* a,const char* b){if(a==failRename||!exists(a)||exists(b))return false;files[b]=files[a];files.erase(a);return true;}
    bool write(const char* p,const uint8_t* b,unsigned n){files[p]={b,b+n-(shortWrite?1:0)};return !shortWrite;}
    bool read(const char* p,uint8_t* b,unsigned n){if(!exists(p)||files[p].size()!=n)return false;std::memcpy(b,files[p].data(),n);if(badRead)b[0]^=1;return true;}
};
int main(){
    AudioPauseGate gate;auto a=gate.request();require(!gate.acknowledged(a),"new pause needs worker acknowledgement");gate.acknowledge();require(gate.acknowledged(a),"worker acknowledges first pause");
    gate.resume();auto b=gate.request();require(!gate.acknowledged(b),"stale first acknowledgement cannot approve second pause");gate.acknowledge();require(gate.acknowledged(b),"worker can acknowledge a new pause without observing intermediate resume");gate.resume();
    for(unsigned i=0;i<10000;++i){auto ticket=gate.request();require(!gate.acknowledged(ticket),"every pause owns a unique ticket");gate.acknowledge();require(gate.acknowledged(ticket),"repeated pauses acknowledge");gate.resume();}
    MemoryFS fs;SamplePatch first,next,got;std::strcpy(first.title,"first");std::strcpy(first.path,"/Samples/a.wav");first.frames=32768;first.rate=24000;first.sound.end=32768;
    next=first;std::strcpy(next.title,"replacement");next.sound.mode=SampleMode::Loop;next.sound.release=150;next.sound.loopRegion=true;next.sound.loopStart=2048;next.sound.loopEnd=8192;next.sound.loopFade=128;
    require(writeSoundRecord(fs,"slot","tmp","bak",first)&&readSoundRecord(fs,"slot",got)&&!std::strcmp(got.title,"first"),"first SD save/read round trip");
    auto baseline=fs;fs.shortWrite=true;require(!writeSoundRecord(fs,"slot","tmp","bak",next),"short metadata write fails");fs.shortWrite=false;require(readSoundRecord(fs,"slot",got)&&!std::strcmp(got.title,"first"),"short write preserves prior sound");
    fs=baseline;fs.badRead=true;require(!writeSoundRecord(fs,"slot","tmp","bak",next),"bad readback cannot commit");fs.badRead=false;require(readSoundRecord(fs,"slot",got)&&!std::strcmp(got.title,"first"),"verification failure preserves prior sound");
    fs=baseline;fs.failRename="tmp";require(!writeSoundRecord(fs,"slot","tmp","bak",next),"commit rename failure reported");require(readSoundRecord(fs,"slot",got)&&!std::strcmp(got.title,"first"),"failed commit rolls prior sound back");
    fs=baseline;require(writeSoundRecord(fs,"slot","tmp","bak",next)&&readSoundRecord(fs,"slot",got)&&!std::strcmp(got.title,"replacement")&&got.sound.mode==SampleMode::Loop&&got.sound.release==150&&got.sound.loopRegion&&got.sound.loopStart==2048&&got.sound.loopEnd==8192&&got.sound.loopFade==128,"overwrite replaces name and held-loop settings");
    require(readSoundRecord(fs,"bak",got)&&!std::strcmp(got.title,"first"),"backup has prior sound");
    fs.files["slot"][0]^=1;fs.failRename="tmp";require(!writeSoundRecord(fs,"slot","tmp","bak",next),"failure with corrupt final reported");require(readSoundRecord(fs,"bak",got)&&!std::strcmp(got.title,"first"),"corrupt final cannot destroy valid recovery backup");
    uint8_t modBytes[SamplePatchBytes];encodeSamplePatch(first,modBytes);modBytes[5]='5';patch32(modBytes+188,crc32(modBytes,188));MemoryFS modFS;modFS.files["slot"]={modBytes,modBytes+192};require(readSoundRecord(modFS,"slot",got)&&got.sound.oscB==0&&got.sound.lfoTarget==0,"192-byte PSMP05 migrates without enabling new effects");
    require(writeSoundRecord(modFS,"slot","tmp","bak",next)&&modFS.files["slot"].size()==SamplePatchBytes&&readSoundRecord(modFS,"bak",got),"PSMP05 transaction migrates with readable old-format backup");
    // PSMP04 remains readable after the PSMP05 envelope extension.
    uint8_t loopBytes[SamplePatchBytes];encodeSamplePatch(first,loopBytes);loopBytes[5]='4';patch32(loopBytes+172,crc32(loopBytes,172));
    MemoryFS loopFS;loopFS.files["slot"]={loopBytes,loopBytes+176};
    require(readSoundRecord(loopFS,"slot",got)&&got.sound.pitchEnv==0&&got.sound.filterEnv==0,"176-byte PSMP04 migrates with envelopes bypassed");
    // A real 160-byte old record must survive replacement and remain readable in .bak.
    uint8_t legacy[SamplePatchBytes];encodeSamplePatch(first,legacy);legacy[5]='3';patch32(legacy+156,crc32(legacy,156));
    MemoryFS old;old.files["slot"]={legacy,legacy+160};require(readSoundRecord(old,"slot",got)&&!got.sound.loopRegion&&got.sound.loopFade==64,"legacy SD record reads with full-crop loop defaults");
    require(writeSoundRecord(old,"slot","tmp","bak",next)&&old.files["slot"].size()==SamplePatchBytes&&readSoundRecord(old,"bak",got)&&!std::strcmp(got.title,"first"),"legacy overwrite commits PSMP06 while preserving 160-byte recovery backup");
    std::puts("PASS: SD save/overwrite/readback/rollback/corruption recovery and repeated audio pause tickets");
}
