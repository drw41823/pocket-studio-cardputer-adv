#include "AudioStream.h"
#include "PlayerMP3.h"
#include "PlayerBrowser.h"
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace pocket;
void require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
void decodeFixture(const char* path,unsigned expectedRate,unsigned expectedChannels){
    std::ifstream file(path,std::ios::binary);std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};require(!bytes.empty(),"MP3 fixture exists");
    auto* decoder=createMP3();require(decoder,"decoder allocates");AudioStream stream;stream.reset(expectedRate);stream.volume=100;stream.playing=true;
    std::vector<int16_t> output;int16_t pcm[2306];size_t at=0;unsigned sourceFrames=0,frames=0;
    auto render=[&](){int16_t block[128];stream.render(block,128);output.insert(output.end(),block,block+128);};
    while(at<bytes.size()){
        pcm[0]=pcm[2305]=1234;MP3Frame info;int n=decodeMP3(decoder,bytes.data()+at,std::min<size_t>(4096,bytes.size()-at),pcm+1,info);
        require(pcm[0]==1234&&pcm[2305]==1234,"decoder output guard samples intact");
        if(!info.bytes)break;require(info.bytes>0&&at+info.bytes<=bytes.size(),"decoder consumption stays in input");at+=info.bytes;
        if(!n)continue;++frames;require(unsigned(info.rate)==expectedRate&&unsigned(info.channels)==expectedChannels,"MP3 rate and channels correct");
        for(int i=0;i<n;++i)pcm[i]=info.channels==2?int16_t((int(pcm[1+i*2])+int(pcm[2+i*2]))/2):pcm[1+i];
        while(stream.free()<unsigned(n))render();require(stream.push(pcm,n),"decoded frame enqueues");sourceFrames+=n;
    }
    stream.eof=true;for(unsigned i=0;stream.playing&&i<3000;++i)render();
    require(!stream.playing&&stream.position==sourceFrames&&frames>20,"MP3 decodes to EOF with every source frame consumed");
    require(output.size()>90000&&output.size()<110000,"resampling yields about two seconds at 48kHz");
    double energy=0;for(auto v:output)energy+=double(v)*v;require(std::sqrt(energy/output.size())>2000,"decoded audio has expected energy");
    require(stream.underruns==0,"prefilled decoder/resampler does not underrun");destroyMP3(decoder);
    std::printf("MP3 %s: %u frames, %zu output samples\n",path,frames,output.size());
}
int main(){
    PlayerBrowser browser;std::vector<std::string> names;
    for(unsigned i=0;i<600;++i)names.push_back("ignored"+std::to_string(i)+".txt");
    for(unsigned i=0;i<103;++i)names.push_back("tone"+std::to_string(i)+".WAV");
    unsigned serviced=0;
    auto scan=[&](){unsigned at=0;browser.readPage([&](const char*& name,bool& dir){if(at==names.size())return false;name=names[at++].c_str();dir=false;return true;},[&](){++serviced;});};
    scan();require(browser.count==48&&browser.hasNext&&std::string(browser.paths[0])=="/tone0.WAV"&&serviced>=37,"directory scan reaches audio beyond 512 ignored entries and services playback");
    browser.selected=47;require(browser.move(1)==PlayerBrowser::Move::Page,"down crosses page boundary");scan();require(browser.offset==48&&std::string(browser.paths[0])=="/tone48.WAV","second page contains next 48 entries");
    browser.selected=47;browser.move(1);scan();require(browser.offset==96&&browser.count==7&&!browser.hasNext,"last partial page reachable without truncating folder");
    browser.selected=6;require(browser.move(1)==PlayerBrowser::Move::None,"end of directory is bounded");browser.selected=0;require(browser.move(-1)==PlayerBrowser::Move::Page,"up crosses back to previous page");scan();require(browser.offset==48&&browser.selected==47,"previous page keeps last entry selected");
    browser.offset=browser.selected=0;unsigned at=0;
    browser.readPage([&](const char*& name,bool& dir){if(at++)return false;name="/A";dir=true;return true;},[](){});
    require(browser.enter()&&std::string(browser.folder)=="/A","enter directory from root");at=0;
    browser.readPage([&](const char*& name,bool& dir){if(at++)return false;name="B";dir=true;return true;},[](){});
    require(browser.enter()&&std::string(browser.folder)=="/A/B"&&browser.parent()&&std::string(browser.folder)=="/A"&&browser.parent()&&std::string(browser.folder)=="/"&&!browser.parent(),"nested navigation and root parent are bounded");
    at=0;std::string tooLong(128,'x');browser.readPage([&](const char*& name,bool& dir){if(at++)return false;name=tooLong.c_str();dir=true;return true;},[](){});require(browser.count==0&&browser.omitted==1,"overlong paths reported without truncation or buffer overflow");

    AudioStream stream;int16_t data[1000];for(unsigned i=0;i<1000;++i)data[i]=10000;
    stream.reset(8000);require(stream.push(data,1000),"8k source queues");int16_t out[128];stream.render(out,128);require(stream.position==0,"paused stream never consumes input");
    stream.playing=true;stream.eof=true;for(unsigned i=0;i<48;++i)stream.render(out,128);require(stream.position==1000&&!stream.playing&&stream.finished,"8k resampling consumes exactly source length");
    stream.reset(48000);require(!stream.push(data,9000),"oversized push rejected before touching source");
    decodeFixture("tests/fixtures/tone-stereo.mp3",44100,2);decodeFixture("tests/fixtures/tone-mono.mp3",22050,1);
    auto* decoder=createMP3();uint8_t bad[4096]{};int16_t pcm[2304];MP3Frame info;require(!decodeMP3(decoder,bad,sizeof bad,pcm,info),"non-MP3 bytes produce no PCM");resetMP3(decoder);destroyMP3(decoder);
    std::puts("PASS: bounded MP3 stereo/mono decoding, source-rate interpolation, EOF, paused position and ring bounds");
}
