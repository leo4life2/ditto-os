#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "ditto_behavior.h"

namespace DittoSleepMarks {
inline float segment(float x,float y,float ax,float ay,float bx,float by) {
  float dx=bx-ax,dy=by-ay;
  float t=std::clamp(((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy),0.f,1.f);
  return std::hypot(x-ax-t*dx,y-ay-t*dy);
}
inline void mouth_drool(uint16_t* pixels,int top,int rows,float seconds,float sleep,int left=0,int stride=410) {
  float fade=DittoBehavior::ease(1.15f,2.10f,seconds)*sleep;
  if(fade<=0 || top>=332 || top+rows<=276)return;
  // One analytic teardrop, rather than a chain of round segments. Its tapered
  // upper tail stays at the mouth; a continuous radius profile and surface
  // normal give the transparent water a smooth silhouette and highlight.
  float phase=(seconds-.9f)*6.2831853f/5.2f;
  float breath=std::sin(phase),height=34.f-2.f*breath,sway=.7f*breath;
  auto tint=[](uint16_t bg,float r,float g,float b,float a) {
    float cr=bg>>11,cg=(bg>>5)&63,cb=bg&31;
    return uint16_t((int(cr+(r-cr)*a)<<11)|(int(cg+(g-cg)*a)<<5)|int(cb+(b-cb)*a));
  };
  for(int y=std::max(top,289);y<std::min(top+rows,329);y++) {
    float t=(y+.5f-289.f)/height;
    if(t<=0 || t>=1)continue;
    // t^1.65 produces a long pointed tail; sqrt(1-t) closes the rounded end.
    // The peak normalization is constant, so shape evaluation is per row.
    float radius=(6.8f+.3f*breath)*3.20929159f*std::pow(t,1.65f)*std::sqrt(1-t);
    float cx=188.5f+(11.5f+sway)*(2*t-t*t);
    float dr=radius*(1.65f/t-.5f/(1-t))/height;
    float dc=(11.5f+sway)*(2-2*t)/height;
    for(int x=std::max(left,183);x<std::min(left+stride,211);x++) {
      float dx=x+.5f-cx,edge=std::clamp(radius+.6f-std::abs(dx),0.f,1.f)*fade;
      if(edge<=0)continue;
      float nx=std::clamp(dx/radius,-1.f,1.f),ny=-(dr+nx*dc);
      float inv=1.f/std::sqrt(1+ny*ny),nz=std::sqrt(std::max(0.f,1-nx*nx))*inv;
      nx*=inv;ny*=inv;
      float light=std::clamp(-.43f*nx-.48f*ny+.76f*nz,0.f,1.f);
      float rim=1-nz;
      float highlight=light*light;highlight*=highlight;highlight*=highlight;
      auto& color=pixels[(y-top)*stride+x-left];
      // The body's live shaded pixels remain visible through the cool tint.
      // Broad reflection and a faint rim read as water without a hard outline.
      color=tint(color,15+5*light,39+9*light,30,(.30f+.12f*rim)*edge);
      color=tint(color,31,61,31,highlight*.60f*edge);
    }
  }
}
// Draw into a clipped native tile. Placement retains the 50 px safe area and
// clears the clock/date; typography is composed afterward, above these marks.
inline void draw(uint16_t* pixels,int top,int rows,float seconds,float sleep,int offset=42,bool drool=false,int left=0,int stride=410) {
  if(sleep<=.01f)return;
  if(drool)mouth_drool(pixels,top,rows,seconds,sleep,left,stride);
  for(int z=0;z<3;z++) {
    float phase=(seconds-.9f)*6.2831853f/5.2f-z*.5f;
    float ox=273+22*z+1.3f*std::sin(phase*.7f),oy=128+offset-24*z-3.5f*std::sin(phase);
    float w=7+2.5f*z,h=9+3.f*z,r=.7f+h*.018f;
    float alpha=(.88f-.14f*z)*DittoBehavior::ease(.65f+z*.22f,1.30f+z*.22f,seconds)*sleep;
    if(alpha<=0 || oy+h+3<top || oy-3>=top+rows)continue;
    for(int y=std::max(top,int(oy)-3);y<std::min(top+rows,int(oy+h)+4);y++)
      for(int x=std::max({50,left,int(ox)-3});x<=std::min({359,left+stride-1,int(ox+w)+3});x++) {
        float d=std::min({segment(x+.5f,y+.5f,ox,oy,ox+w,oy),segment(x+.5f,y+.5f,ox+w,oy,ox,oy+h),segment(x+.5f,y+.5f,ox,oy+h,ox+w,oy+h)});
        float a=std::clamp(r+.5f-d,0.f,1.f)*alpha;
        auto& p=pixels[(y-top)*stride+x-left];float red=p>>11,green=(p>>5)&63,blue=p&31;
        p=(int(red+(27-red)*a)<<11)|(int(green+(48-green)*a)<<5)|int(blue+(31-blue)*a);
      }
  }
}
}
