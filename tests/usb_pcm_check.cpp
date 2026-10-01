#include "usb_pcm.h"
#include <cassert>
#include <cstdio>
#include <cmath>
// Test 60 s at both clock extremes, with delayed ADC delivery and USB service
// gaps. This exercises actual producer/consumer schedules, not canned outputs.
static void run(double clock_ratio) {
 UsbPcm q;int16_t in[320],out[16];double produced=0;unsigned phase=0,drops=0,underflows=0;
 bool sounded=false;int first=-1;
 for(int ms=0;ms<60000;ms++) {
  produced+=16*clock_ratio;
  // A 7 ms scheduling stall every 137 ms, followed by catching up.
  if(ms%137>=7)while(produced>=320) {
   for(auto& v:in)v=int16_t(std::sin((phase++)*.075)*12000);
   if(!q.push(in,320))drops++;produced-=320;
  }
  // One missed USB service slot every 503 ms. No stale samples may accumulate.
  if(ms%503==502)continue;
  if(!q.pop(out))underflows++;
  for(auto v:out){assert(std::abs(int(v))<=12000);if(v && !sounded){first=ms;sounded=true;}}
 }
 assert(first>=55 && first<100);assert(drops==0);assert(underflows==0);
 // An idle endpoint clears the old tail before a new capture opens.
 q.reset();q.pop(out);for(auto v:out)assert(v==0);
 for(auto& v:in)v=9000;q.push(in,320);q.push(in,320);q.push(in,320);q.pop(out);for(auto v:out)assert(v==9000);
 printf("clock %.4f: onset %d ms, no drop/underflow, restart clean\n",clock_ratio,first);
}
int main(){run(.999);run(1.);run(1.001);}
