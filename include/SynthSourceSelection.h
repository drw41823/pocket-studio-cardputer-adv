#pragma once
namespace pocket {
// Never probe or mount SD as a side effect of cycling through unavailable SMPs.
template<class LoadSlot> unsigned nextSynthSource(unsigned current,bool sdReady,LoadSlot load) {
    for(unsigned attempt=0;attempt<8;++attempt){
        current=(current+1)%8;
        if(current<4)return current;
        if(sdReady&&load(current-4))return current;
    }
    return 0;
}
}
