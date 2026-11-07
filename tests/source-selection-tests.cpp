#include "SynthSourceSelection.h"
#include <cassert>
#include <cstdio>
using namespace pocket;
int main(){
 unsigned calls=0;auto forbidden=[&](unsigned){++calls;return false;};
 for(unsigned i=0;i<10000;++i)for(unsigned current=0;current<8;++current){
  unsigned next=nextSynthSource(current,false,forbidden);assert(next<4);
  assert(next==(current<3?current+1:0));
 }assert(calls==0);
 for(unsigned wanted=0;wanted<4;++wanted){unsigned count=0;assert(nextSynthSource(3,true,[&](unsigned s){++count;return s==wanted;})==wanted+4);assert(count==wanted+1);}
 calls=0;assert(nextSynthSource(3,true,forbidden)==0&&calls==4);
 calls=0;bool ready=true;assert(nextSynthSource(3,ready,[&](unsigned){++calls;ready=false;return false;})==0&&calls==4);
 std::puts("PASS: no-SD cycles never call storage, basic fallback, all 4 SD slots, missing-slot fallback");
}
