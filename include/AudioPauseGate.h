#pragma once
#include <atomic>
#include <cstdint>
namespace pocket {
class AudioPauseGate {
    std::atomic<uint32_t> desired{0},ack{0};
    uint32_t serial=0; // UI task only
public:
    uint32_t request(){if(!++serial)++serial;desired.store(serial);return serial;}
    void resume(){desired.store(0);}
    bool requested()const{return desired.load()!=0;}
    void acknowledge(){auto id=desired.load();if(id)ack.store(id);}
    bool acknowledged(uint32_t id)const{return ack.load()==id;}
};
}
