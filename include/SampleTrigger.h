#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pocket {
// The pre-roll ring lives inside the already allocated take buffer.
class SampleTrigger {
public:
    void reset(int16_t* buffer,unsigned frames,unsigned sensitivity=1) {data=buffer;capacity=frames;write=count=0;noise=8;level=sensitivity;peak=threshold=onset=0;}
    bool observe(const int16_t* source,unsigned n,bool armed) {
        if(!data||!capacity||!n)return false;
        int64_t total=0;for(unsigned i=0;i<n;++i)total+=source[i];int mean=int(total/n);
        peak=0;double energy=0;
        for(unsigned i=0;i<n;++i){int delta=int(source[i])-mean;peak=std::max(peak,unsigned(std::abs(delta)));energy+=double(delta)*delta;data[write]=source[i];write=(write+1)%capacity;count=std::min(capacity,count+1);}
        const unsigned factors[]={4,6,10},floors[]={24,64,128};unsigned setting=std::min(level,2u);
        threshold=std::min(65535u,std::max(floors[setting],unsigned(noise*factors[setting])));
        bool hit=armed&&peak>threshold;
        if(hit){unsigned at=0;while(at<n&&unsigned(std::abs(int(source[at])-mean))<=threshold)++at;onset=count-std::min(count,n)+std::min(at,count-1);}
        else {float rms=std::sqrt(energy/n);noise+=(rms-noise)*(armed?.02f:.1f);}
        return hit;
    }
    unsigned commit(){if(data&&count==capacity)std::rotate(data,data+write,data+capacity);return count;}
    unsigned peak=0,threshold=0,onset=0,level=1;
    float noise=8;
private:
    int16_t* data=nullptr;unsigned capacity=0,write=0,count=0;
};
}
