#pragma once
#include <cstring>
#include <cstdio>
namespace pocket {
struct PlayerBrowser {
    static constexpr unsigned Capacity=48,PathBytes=128;
    char paths[Capacity][PathBytes]{},folder[PathBytes]="/";
    bool directories[Capacity]{},hasNext=false;
    unsigned count=0,selected=0,offset=0,omitted=0;
    static bool extension(const char* name,const char* ext) {
        unsigned n=std::strlen(name),m=std::strlen(ext);if(n<m)return false;
        for(unsigned i=0;i<m;++i){char c=name[n-m+i];if(c>='A'&&c<='Z')c+=32;if(c!=ext[i])return false;}return true;
    }
    static bool audio(const char* name){return extension(name,".wav")||extension(name,".mp3");}
    // Iterate one directory, retaining only one page. Every folder is browsable;
    // there is no fixed list of music directories or 512-entry scan cutoff.
    template<class Next,class Service> void readPage(Next next,Service service) {
        count=omitted=0;hasNext=false;unsigned eligible=0,visited=0;const char* name;bool directory;
        while(next(name,directory)) {
            if(++visited%16==0)service();
            const char* base=std::strrchr(name,'/');base=base?base+1:name;
            if(!*base||!std::strcmp(base,".")||!std::strcmp(base,"..")||(!directory&&!audio(base)))continue;
            unsigned length=std::strlen(folder)+std::strlen(base)+(std::strcmp(folder,"/")?1:0);
            if(length>=PathBytes){++omitted;continue;}
            if(eligible++<offset)continue;
            if(count==Capacity){hasNext=true;break;}
            std::snprintf(paths[count],PathBytes,"%s%s%s",folder,std::strcmp(folder,"/")?"/":"",base);
            directories[count++]=directory;
        }
        if(selected>=count)selected=count?count-1:0;
    }
    enum class Move { None,Selection,Page };
    Move move(int direction) {
        if(direction>0){if(selected+1<count){++selected;return Move::Selection;}if(hasNext){offset+=Capacity;selected=0;return Move::Page;}}
        else {if(selected){--selected;return Move::Selection;}if(offset){offset=offset>=Capacity?offset-Capacity:0;selected=Capacity-1;return Move::Page;}}
        return Move::None;
    }
    bool enter() {if(selected>=count||!directories[selected])return false;std::strcpy(folder,paths[selected]);offset=selected=0;return true;}
    bool parent() {if(!std::strcmp(folder,"/"))return false;char* slash=std::strrchr(folder,'/');if(slash==folder)folder[1]=0;else if(slash)*slash=0;else std::strcpy(folder,"/");offset=selected=0;return true;}
};
}
