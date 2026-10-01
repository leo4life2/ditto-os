#include "ditto_sleep_marks.h"
#include <vector>
#include <cassert>
#include <cstdio>
int main() {
  constexpr int w=410,h=502;
  std::vector<uint16_t> a(w*h,0x582e),b(w*h,0x582e),tiled(w*h,0x582e);
  DittoSleepMarks::draw(a.data(),0,h,2.4f,1,60,true);
  DittoSleepMarks::draw(b.data(),0,h,3.5f,1,60,true);
  int changed=0;
  for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(a[y*w+x]!=b[y*w+x]) {
    changed++;assert(x>=50&&x<360);assert((y>=120&&y<216)||(y>=276&&y<332));
  }
  assert(changed>100&&changed<3000);
  for(int y=0;y<h;y+=16)DittoSleepMarks::draw(tiled.data()+y*w,y,std::min(16,h-y),2.4f,1,60,true);
  assert(a==tiled);
  for(const auto& rect: {std::vector<int>{268,120,76,104},std::vector<int>{180,276,40,56}}) {
    int x=rect[0],y=rect[1],rw=rect[2],rh=rect[3];std::vector<uint16_t> patch(rw*rh,0x582e);
    DittoSleepMarks::draw(patch.data(),y,rh,2.4f,1,60,true,x,rw);
    for(int yy=0;yy<rh;yy++)for(int xx=0;xx<rw;xx++)assert(patch[yy*rw+xx]==a[(y+yy)*w+x+xx]);
  }
  // Restore from immutable background every time: arbitrary revisits are
  // pixel-identical and cannot accumulate outlines or alpha darkening.
  std::fill(tiled.begin(),tiled.end(),0x582e);
  DittoSleepMarks::draw(tiled.data(),0,h,2.4f,1,60,true);assert(a==tiled);
  for(int i=0;i<720;i++) {
    std::fill(tiled.begin(),tiled.end(),0x582e);
    DittoSleepMarks::draw(tiled.data(),0,h,2+i/12.f,1,60,true);
    for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(tiled[y*w+x]!=0x582e)
      assert((y>=120&&y<216)||(y>=276&&y<332));
  }
  puts("60-second overlay bounds, tile equivalence, smooth changing frames, immutable background and no trails passed.");
}
