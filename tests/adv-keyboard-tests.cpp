#include "AdvKeyQueue.h"
#include <deque>
#include <cstdio>
#include <cstdlib>
using namespace pocket;
void check(bool ok,const char* text){if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
struct Bus {
 std::deque<uint8_t> fifo;uint8_t status=0;bool fail=false,refill=false,lateOverflow=false;unsigned reads=0,statusReads=0;
 void push(uint8_t e){if(fifo.size()==10){status|=8;return;}fifo.push_back(e);status|=1;}
 bool read(uint8_t reg,uint8_t& v){if(fail)return false;if(reg==2){if(++statusReads==2&&lateOverflow)for(unsigned i=0;i<12;++i)push(0x81);v=status;}else if(reg==3)v=fifo.size();else if(reg==4){++reads;v=fifo.empty()?0:fifo.front();if(!fifo.empty())fifo.pop_front();if(refill)push(0x81);}else return false;return true;}
 bool write(uint8_t reg,uint8_t v){if(fail)return false;status&=~v;if(!fifo.empty())status|=1;return reg==2;}
};
int main(){
 bool seen[56]{};for(unsigned row=0;row<7;++row)for(unsigned col=0;col<8;++col){int index=advKeyIndex(128+row*10+col+1);check(index>=0&&index<56&&!seen[index],"all matrix coordinates map once inside 14x4 bounds");seen[index]=true;}
 for(unsigned e:{0u,128u,9u,10u,19u,20u,69u,127u,255u})check(advKeyIndex(e)<0,"empty/GPIO/gap/out-of-range event rejected");
 AdvKeyPump pump;Bus bus;bus.push(0x81);bus.push(1);pump.service(bus);check(bus.fifo.empty()&&pump.queue.count==2,"hardware FIFO drained without waiting for any IRQ edge");
 pump.queue.advance();check(pump.queue.held==1,"quick tap down survives bulk drain");pump.queue.advance();check(pump.queue.held==0,"quick tap release delivered on next scan");
 for(unsigned n=0;n<1000;++n){for(unsigned id=1;id<=4;++id)bus.push(128+id);for(unsigned id=1;id<=4;++id)bus.push(id);pump.service(bus);for(unsigned i=0;i<4;++i)pump.queue.advance();check(pump.queue.held!=0,"four-note chord reaches UI");for(unsigned i=0;i<4;++i)pump.queue.advance();check(!pump.queue.held,"repeated chord always releases");}
 check(!pump.overflows&&!pump.errors&&!pump.queue.softwareOverflows,"thousands of note edges do not leak queued/held state");
 bus.push(0x81);pump.service(bus);pump.queue.advance();check(pump.queue.held==1,"note held before overflow");for(unsigned i=0;i<12;++i)bus.push(0x82);pump.service(bus);check(pump.overflows==1&&!pump.queue.held&&!pump.queue.count&&!pump.resync&&bus.fifo.empty()&&bus.status==0,"FIFO overflow drops uncertain state and clears interrupt flags");
 bus.push(0x81);pump.service(bus);pump.queue.advance();check(pump.queue.held==1,"keyboard resumes after hardware overflow");bus.push(1);pump.service(bus);pump.queue.advance();
 AdvKeyPump late;Bus race;race.lateOverflow=true;late.service(race);check(late.resync&&late.overflows==1,"overflow arriving during drain is detected");late.service(race);check(!late.resync&&race.fifo.empty()&&!late.queue.held,"late overflow resynchronises on next bounded poll");
 AdvKeyPump bounded;Bus endless;endless.push(0x81);endless.refill=true;bounded.service(endless);check(endless.reads==32,"continuously arriving events cannot monopolise UI thread");
 AdvKeyQueue full;for(unsigned i=0;i<128;++i)check(full.push(0x81),"software event queue bounded capacity");full.advance();full.push(0x82);check(!full.push(0x83)&&!full.held&&!full.count&&full.softwareOverflows==1,"software overflow safely releases and resets");
 bus.fail=true;unsigned errors=pump.errors;pump.service(bus);check(pump.errors==errors+1,"I2C errors reported without decoding garbage");bus.fail=false;bus.push(0x81);pump.service(bus);pump.queue.advance();check(pump.queue.held==1,"transient I2C failure does not prevent future polling");
 std::puts("PASS: ADV keyboard mapping, lost-IRQ polling, tap/chord edges, 8000 events, hardware/software overflow recovery, bounded drainage and I2C errors");
}
