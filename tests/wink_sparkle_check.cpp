#include "ditto_wink_sparkle.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <limits>
constexpr int W=178,H=90;
using Pixels=std::array<uint16_t,W*H>;
Pixels render(DittoWinkSparkle& star,int tile,uint16_t z=0) {
  Pixels pixels;pixels.fill(0x8594);
  for(int top=0;top<H;top+=tile) {
    int rows=std::min(tile,H-top);std::array<uint16_t,W*H> depth;depth.fill(z);
    a3d::SoftBackend renderer;renderer.setTarget(pixels.data()+top*W,W,rows,W);
    renderer.setOffset(0,top);renderer.setZBuffer(depth.data());star.draw(renderer,top,rows);
  }
  return pixels;
}
int main() {
  DittoWinkSparkle star;
  float eye[]={0,0},identity[16]={};identity[0]=identity[5]=identity[10]=identity[15]=1;
  auto deform=[](const float* p,float* q){std::copy(p,p+3,q);};
  Pixels quiet;quiet.fill(0x8594);
  star.pose(true,.70f,eye,deform);assert(!star.project(identity,identity,W,H));
  assert(render(star,18)==quiet);
  star.pose(true,.96f,eye,deform);assert(star.project(identity,identity,W,H));
  auto shown=render(star,H);assert(shown!=quiet);
  assert(shown==render(star,1));assert(shown==render(star,18));
  assert(render(star,18,65535)==quiet); // Scene depth is respected.
  star.pose(true,1.10f,eye,deform);assert(star.project(identity,identity,W,H));
  assert(render(star,18)!=shown); // The star actually turns, not just fades.
  star.pose(false,1.10f,eye,deform);assert(!star.project(identity,identity,W,H));
  assert(render(star,18)==quiet); // Touch, sleep and canceled clips leave no mark.
  star.pose(true,1.40f,eye,deform);assert(!star.project(identity,identity,W,H));
  star.pose(true,1.f,eye,deform);
  identity[15]=-.1f;assert(!star.project(identity,identity,W,H));
  identity[15]=1;star.center[0]=std::numeric_limits<float>::quiet_NaN();
  assert(!star.project(identity,identity,W,H));assert(render(star,18)==quiet);
  puts("Sparkle rotation, lifetime, cancellation, depth rejection and exact tile boundaries passed.");
}
