#pragma once
#include <atomic>
#include <algorithm>
#include <cstdint>
namespace pocket {
// One UI producer and one audio-task consumer. Reset only while acknowledged paused.
class AudioStream {
    static constexpr unsigned Capacity=8192;
    int16_t data[Capacity]{};std::atomic<uint32_t> read{0},write{0};
    unsigned phase=0;float gain=0,last=0;
public:
    std::atomic<bool> playing{false},eof{false},finished{false};
    std::atomic<unsigned> rate{48000},volume{65},position{0},underruns{0};
    unsigned size()const{return write.load(std::memory_order_acquire)-read.load(std::memory_order_acquire);}
    unsigned free()const{return Capacity-size();}
    void reset(unsigned hz,unsigned at=0){read=0;write=0;phase=0;gain=last=0;rate=hz;position=at;eof=false;finished=false;playing=false;underruns=0;}
    bool push(const int16_t* source,unsigned n){auto w=write.load();if(n>Capacity-(w-read.load(std::memory_order_acquire)))return false;for(unsigned i=0;i<n;++i)data[(w+i)%Capacity]=source[i];write.store(w+n,std::memory_order_release);return true;}
    void render(int16_t* out,unsigned n){
        bool starved=false;
        for(unsigned i=0;i<n;++i){
            auto r=read.load();unsigned count=write.load(std::memory_order_acquire)-r;bool active=playing.load();
            if(active&&count&&(count>=2||eof.load())){
                float a=data[r%Capacity],b=count>=2?data[(r+1)%Capacity]:a;
                float target=std::min(volume.load(),100u)/100.f;gain+=(target-gain)*.004f;
                last=(a+(b-a)*phase/48000.f)*gain;phase+=std::min(48000u,std::max(8000u,rate.load()));
                if(phase>=48000){phase-=48000;read.store(r+1,std::memory_order_release);++position;}
            }else{
                last*=.96f;
                if(active&&!count&&eof.load()){playing=false;finished=true;}
                else if(active&&!eof.load())starved=true;
            }
            out[i]=int16_t(std::max(-32112.f,std::min(32112.f,last)));
        }
        if(starved)++underruns;
    }
};
}
