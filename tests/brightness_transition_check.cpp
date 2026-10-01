#include "../main/brightness_transition.h"
#include <cassert>
#include <cstdio>
int main() {
  BrightnessTransition b;
  assert(b.update(false,55,0)==55);
  assert(b.update(true,55,100000)==55);
  int previous=55;
  for(int t=0;t<=1600000;t+=10000) {
    int x=b.update(true,55,100000+t);assert(x<=previous && x>=10);previous=x;
    if(t==800000)assert(x==33);
  }
  assert(previous==10 && !b.active(1700000));
  assert(b.update(false,55,2000000)==10);
  assert(b.update(false,55,2060000)>40);
  assert(b.update(false,55,2180000)==55);
  assert(b.update(false,82,2200000)==82); // ordinary brightness stays immediate
  for(int t=20000;t<1600000;t+=57000) {
    BrightnessTransition interrupted;interrupted.update(false,55,0);
    interrupted.update(true,55,1000);int level=interrupted.update(true,55,1000+t);
    assert(interrupted.update(false,55,1000+t)==level);
    assert(interrupted.update(false,55,1000+t+180000)==55);
  }
  // Elapsed time, not frame count, determines brightness even if a frame stalls.
  b.update(true,82,3000000);assert(b.update(true,82,5000000)==10);
  puts("Smooth monotonic 1.6 s dim, fast 180 ms wake, interrupted fades and manual settings passed.");
}
