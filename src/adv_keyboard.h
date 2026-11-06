#pragma once
#include "AdvKeyQueue.h"
#include <utility/Keyboard/KeyboardReader/KeyboardReader.h>
#include <utility/Adafruit_TCA8418/Adafruit_TCA8418.h>
// Project-owned reader injected through M5Cardputer's public API. No SDK patch.
class StudioAdvKeyboard : public KeyboardReader {
    Adafruit_TCA8418 device;
    uint32_t lastPoll=0;uint64_t published=~uint64_t(0);
    struct Bus {
        Adafruit_TCA8418& device;
        bool read(uint8_t reg,uint8_t& value){return device.readRegister(reg,&value,1);}
        bool write(uint8_t reg,uint8_t value){return device.writeRegister8(reg,value);}
    };
    void publishKeys(){
        if(published==pump.queue.held)return;published=pump.queue.held;_key_list.clear();
        for(unsigned i=0;i<56;++i)if(published&(uint64_t(1)<<i)){Point2D_t point;point.x=i%14;point.y=i/14;_key_list.push_back(point);}
    }
public:
    pocket::AdvKeyPump pump;bool ready=false;
    void begin() override {
        _key_list.reserve(56);Bus bus{device};uint8_t config=0;
        // M5Cardputer.begin has already set up the matrix. Disable its edge IRQs
        // and unrelated GPIO event sources; keep the matrix/debounce settings.
        ready=bus.read(1,config)&&bus.write(1,config&~15);
        for(uint8_t reg:{uint8_t(0x20),uint8_t(0x21),uint8_t(0x22),uint8_t(0x1a),uint8_t(0x1b),uint8_t(0x1c)})
            if(!bus.write(reg,0))ready=false;
        lastPoll=millis()-4;
        if(ready)pollHardware();else ++pump.errors;
        publishKeys();
    }
    void pollHardware(){
        if(!ready||uint32_t(millis()-lastPoll)<4)return;
        lastPoll=millis();Bus bus{device};pump.service(bus);
    }
    void update() override {pollHardware();pump.queue.advance();publishKeys();}
};
