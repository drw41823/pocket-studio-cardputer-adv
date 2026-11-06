#pragma once
namespace pocket {
inline unsigned samplerNextPage(unsigned current,bool reverse,bool basic){
    do{current=(current+(reverse?3:1))%4;}while(basic&&current==1);return current;
}
inline unsigned samplerFileCount(bool basic){return basic?5:7;}
inline unsigned samplerFileActionAt(unsigned row,bool basic){
    static constexpr unsigned baseActions[5]={1,2,10,0,9};
    return basic?baseActions[row%5]:(row<3?row:row+3);
}
}
