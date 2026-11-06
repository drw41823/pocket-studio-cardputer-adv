#pragma once
#include <cstdint>
namespace pocket {
// TCA8418: seven matrix rows, eight columns -> Cardputer's 14 x 4 keys.
// Invalid/empty/GPIO events must never index Keyboard_Class's key map.
inline int advKeyIndex(uint8_t event){
    unsigned id=event&127;if(!id||id>68)return -1;
    unsigned row=(id-1)/10,col=(id-1)%10;
    if(row>=7||col>=8)return -1;
    return int((col%4)*14+row*2+(col>=4));
}
struct AdvKeyQueue {
    static constexpr unsigned Capacity=128;
    uint8_t events[Capacity]{};unsigned head=0,count=0;
    uint64_t held=0;unsigned invalid=0,softwareOverflows=0;
    void reset(){head=count=0;held=0;}
    bool push(uint8_t event){
        if(advKeyIndex(event)<0){if(event)++invalid;return true;}
        if(count==Capacity){reset();++softwareOverflows;return false;}
        events[(head+count)%Capacity]=event;++count;return true;
    }
    void advance(){
        if(!count)return;uint8_t event=events[head];head=(head+1)%Capacity;--count;
        uint64_t bit=uint64_t(1)<<advKeyIndex(event);
        if(event&128)held|=bit;else held&=~bit;
    }
};
// Bus.read(reg,value) and Bus.write(reg,value) report I2C errors explicitly.
// Hardware is polled independently of IRQ edges; every service call is bounded.
struct AdvKeyPump {
    AdvKeyQueue queue;unsigned events=0,overflows=0,errors=0,polls=0;
    uint8_t lastStatus=0,lastCount=0;bool resync=false;
    void recover(){queue.reset();if(!resync)++overflows;resync=true;}
    template<class Bus> void service(Bus& bus){
        ++polls;uint8_t status=0,count=0;
        if(!bus.read(2,status)||!bus.read(3,count)){++errors;return;}
        lastStatus=status;count&=15;
        if(status&8)recover();
        for(unsigned budget=0;count&&budget<32;++budget){
            uint8_t event=0;if(!bus.read(4,event)){++errors;return;}++events;
            if(!resync&&!queue.push(event))resync=true;
            if(!bus.read(3,count)){++errors;return;}count&=15;
        }
        if(!bus.read(2,status)||!bus.read(3,count)){++errors;return;}
        lastStatus=status;lastCount=count&15;
        if(status&8)recover();
        if(!lastCount){
            if(!bus.write(2,status&31)){++errors;return;}
            resync=false;
        }
    }
};
}
