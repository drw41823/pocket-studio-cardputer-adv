#pragma once
#include <algorithm>
namespace pocket {
struct ToneNavigation {
    unsigned group=0,row=0,remembered[8]{};bool groups=true,basic=false;
    static unsigned count(unsigned g){return g==3?2:4;}
    static unsigned parameter(unsigned g,unsigned r){
        static constexpr unsigned map[8][4]={{0,1,6,7},{2,3,4,5},{8,9,18,19},{10,11,10,11},{12,13,14,15},{20,21,16,17},{22,23,24,25},{26,27,28,29}};
        return map[std::min(g,7u)][std::min(r,count(std::min(g,7u))-1)];
    }
    unsigned rows()const{return basic&&group==0?2:count(group);}
    unsigned parameterAt(unsigned r)const{return basic&&group==0?6+std::min(r,1u):parameter(group,r);}
    unsigned field()const{return parameterAt(row);}
    void setBasic(bool value){basic=value;if(basic&&group==4)group=5;row=std::min(row,rows()-1);groups=true;}
    bool enabled(unsigned g)const{return g<8&&(!basic||g!=4)&&(basic||g!=6);}
    void focusGroups(){remembered[group]=row;groups=true;}
    void enterParameters(){row=std::min(remembered[group],rows()-1);groups=false;}
    void select(unsigned field){for(unsigned g=0;g<8;++g){if(!enabled(g))continue;unsigned n=basic&&g==0?2:count(g);for(unsigned r=0;r<n;++r)if((basic&&g==0?6+r:parameter(g,r))==field){group=g;row=r;remembered[g]=r;groups=false;return;}}}
    int move(bool up,bool down,bool left,bool right,bool enter){
        if(groups){
            if(left||right){do{group=(group+(right?1:7))%8;}while(!enabled(group));row=std::min(remembered[group],rows()-1);}
            if(down||enter)enterParameters();return 0;
        }
        if(enter){focusGroups();return 0;}
        if(up){if(!row)focusGroups();else --row;}
        if(down)row=std::min(rows()-1,row+1);
        remembered[group]=row;
        return !groups&&(left||right)?(right?1:-1):0;
    }
    bool back(){if(groups)return false;focusGroups();return true;}
};
}
