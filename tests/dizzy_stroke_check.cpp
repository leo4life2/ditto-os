#include "backends/soft/a3d_soft_backend.h"
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <cstdio>
using a3d::SoftBackend;
constexpr int W=178,H=37;
using Pixels=std::array<uint16_t,W*H>;
Pixels render(int tile,const SoftBackend::StrokePoint* path,int n,uint16_t z=0) {
 Pixels out;out.fill(0x8594);
 for(int top=0;top<H;top+=tile) {
  int rows=std::min(tile,H-top);
  SoftBackend b;std::array<uint16_t,W*H> depth;depth.fill(z);
  b.setTarget(out.data()+top*W,W,rows,W);b.setOffset(0,top);b.setZBuffer(depth.data());
  b.drawProjectedStroke(path,n,0x1062,.83f);
 }
 return out;
}
int main() {
 const SoftBackend::StrokePoint p[]={{-40,1,.4f,2},{80,17,.4f,1.5f},{200,34,.4f,2}};
 auto full=render(H,p,3);assert(full==render(1,p,3));assert(full==render(18,p,3));
 int changed=0,edge=0;for(auto c:full){changed+=c!=0x8594;edge+=c!=0x8594&&c!=0x1062;}
 assert(changed>300 && edge>100);
 const SoftBackend::StrokePoint repeated[]={p[0],p[1],p[2],p[1],p[0]};
 assert(full==render(18,repeated,5)); // union coverage, not dark overlaps
 auto hidden=render(18,p,3,65535);for(auto c:hidden)assert(c==0x8594);
 const float nan=std::numeric_limits<float>::quiet_NaN();
 const SoftBackend::StrokePoint bad[]={{nan,0,0,1},{1e30f,1e30f,.3f,2},{1e30f,-1e30f,.3f,2}};
 auto clipped=render(18,bad,3);for(auto c:clipped)assert(c==0x8594);
 puts("Stroke coverage, transparent joins, depth rejection and tile/clipping boundaries passed.");
}
