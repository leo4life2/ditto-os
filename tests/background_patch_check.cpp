#include "backends/soft/a3d_soft_backend.h"
#include <vector>
#include <cassert>
#include <cstdio>
int main(){
 int cases=0;
 for(int width=1;width<=43;width++)for(int alignment=0;alignment<2;alignment++)for(int mode=0;mode<5;mode++)for(int left=-3;left<width+4;left+=3){
  const int height=5,stride=width+3,ox=7,oy=11;
  std::vector<uint16_t> color(stride*height+2,0x517a),expected=color,z(width*height+4,0x2468);
  auto* depth=z.data()+alignment;auto* pixels=color.data()+1;
  for(int y=0;y<height;y++)for(int x=0;x<width;x++){
   unsigned seed=(x*1664525u+y*1013904223u);
   depth[y*width+x]=mode==0?0:mode==1?0x7fff:mode==2?uint16_t(1u<<(x%16)):seed%3?uint16_t(seed|1):0;
  }
  auto original_z=z;
  a3d::SoftBackend r;r.setTarget(pixels,width,height,stride);r.setOffset(ox,oy);r.setZBuffer(mode==4?nullptr:depth);
  auto shade=[](uint16_t b,int x,int y){return uint16_t(b^(x*311+y*917));};
  int top=oy-1,w=17,h=5;
  r.drawBackgroundPatch(ox+left,top,w,h,shade);
  for(int y=0;y<height;y++)for(int x=0;x<width;x++)if(x>=left&&x<left+w&&y+oy>=top&&y+oy<top+h&&(mode==4||depth[y*width+x]==0))
   expected[1+y*stride+x]=shade(expected[1+y*stride+x],x+ox,y+oy);
  assert(color==expected);assert(z==original_z);cases++;
 }
 printf("%d background/depth cases: occupied black pixels preserved; zero depth shaded; odd widths, alignment, offsets, clipping, absent depth and untouched guards passed.\n",cases);
}
