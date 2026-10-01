#include "ditto_autoplay.h"
#include <cassert>
#include <cstdio>
using namespace DittoAutoplay;
int main(){
 Scheduler s(71);auto d=s.tick(1,true,false,0,false);assert(d.clip<0);
 const auto due=s.hello_at;assert(due>=25000001&&due<=45000001);
 assert(s.tick(due-1,true,false,0,false).clip<0);
 assert(s.tick(due,true,false,0,false).clip==Avatar::Wave);
 assert(s.hello_count==1 && s.owns && !s.reaction);
 assert(s.tick(due+1,true,true,0,false).clip<0);
 assert(s.tick(due+2,false,true,0,false).cancel); // touch/recording/sleep interrupts
 assert(s.hello_at==0&&!s.owns);
 assert(s.tick(due+3,true,false,0,false).clip<0); // no instant repeat
 bool seen[Avatar::ClipCount]={};int frequency[Avatar::ClipCount]={};int prev=-1;
 for(unsigned e=1;e<=10000;e++){
   d=s.tick(due+e+10,true,true,e,false);
   assert(s.owns && s.reaction);
   assert(d.clip>=0&&d.clip!=prev);prev=d.clip;seen[d.clip]=true;
   frequency[d.clip]++;
   bool allowed=false;for(int c:reactions)allowed|=c==d.clip;assert(allowed);
   assert(s.tick(due+e+10,true,true,e,false).clip<0); // no duplicate
 }
 for(int c:reactions)assert(seen[c]);assert(s.reaction_count==10000);
 assert(frequency[Avatar::Wink]>1700 && frequency[Avatar::Wink]<2100);
 for(int c:reactions)if(c!=Avatar::Wink)assert(frequency[Avatar::Wink]>frequency[c]);
 printf("Weighted sample: Wink %d/10000, other clips",frequency[Avatar::Wink]);
 for(int c:reactions)if(c!=Avatar::Wink)printf(" %d",frequency[c]);puts("");
 assert(s.tick(due+20000,false,false,10001,false).clip<0); // suppressed event consumed
 assert(s.tick(due+20001,true,false,10001,false).clip<0); // never delayed until wake
 assert(s.tick(due+20002,true,false,10002,true).clip<0); // explicit emote priority
 assert(!s.owns);
 assert(!s.tick(due+20003,false,true,10002,false).cancel); // don't cancel manual clip
 for(int i=0;i<1000;i++)assert(s.tick(due+30000+i,false,false,10002,false).clip<0);
 assert(!s.hello_at);
 puts("PASS: idle deadline, touch/listening/sleep cancellation, all seven reactions, 10000 nonrepeating picks, dedup and manual priority");
}
