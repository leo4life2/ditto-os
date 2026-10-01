#include "ditto_dizzy_ink.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
constexpr int W=178,H=90;
using Pixels=std::array<uint16_t,W*H>;
Pixels render(const DittoDizzyInk& ink,int tile,uint16_t z=0) {
  Pixels pixels;pixels.fill(0x8594);
  for(int top=0;top<H;top+=tile) {
    int rows=std::min(tile,H-top);std::array<uint16_t,W*H> depth;depth.fill(z);
    a3d::SoftBackend r;r.setTarget(pixels.data()+top*W,W,rows,W);
    r.setOffset(0,top);r.setZBuffer(depth.data());r.setCulling(0);ink.draw(r,top,rows);
  }
  return pixels;
}
int main() {
  DittoDizzyInk ink;assert(ink.begin(malloc));
  float identity[16]={};identity[0]=identity[5]=identity[10]=identity[15]=1;
  float projection[16]={};projection[0]=projection[5]=projection[10]=projection[15]=3;
  float eyes[2][2]={{-.5f,.5f},{.5f,.5f}};
  auto unchanged=[](const float* p,float* out){std::copy(p,p+3,out);};
  auto mouth=[](int v,float* out){float u=-1.f+2.f*(v/2)/24.f;out[0]=.6f*u;out[1]=(v%2?1.f:-1.f)*.2f*std::sqrt(std::max(0.f,1-u*u));out[2]=0;};
  Pixels quiet;quiet.fill(0x8594);
  ink.pose(0,0,eyes,unchanged);ink.poseMouth(0,mouth);
  assert(!ink.project(identity,projection,W,H));assert(render(ink,18)==quiet);
  ink.poseMouth(1,mouth);assert(ink.project(identity,projection,W,H));
  auto pink=render(ink,H);assert(pink!=quiet);
  assert(pink==render(ink,1));assert(pink==render(ink,18));
  assert(render(ink,18,65535)==quiet);
  auto center=pink[(H/2)*W+W/2];assert((center>>11)>24 && (center&31)>14);
  for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(y<33||y>56||x<31||x>146)assert(pink[y*W+x]==0x8594);
  // Returning to a line clears the fill; eyes can still become dizzy without
  // resurrecting a stale mouth path or reading uninitialized eye projections.
  ink.poseMouth(0,mouth);assert(!ink.project(identity,projection,W,H));assert(render(ink,18)==quiet);
  ink.pose(1,1,eyes,unchanged);assert(ink.project(identity,projection,W,H));
  auto spiral=render(ink,18);assert(spiral!=quiet);assert(spiral[(H/2)*W+W/2]==0x8594);
  assert(spiral==render(ink,1));assert(spiral==render(ink,H));
  free(ink.rest);
  puts("Independent mouth/eyes, pink fill, closure, depth rejection and exact tiling passed.");
}
