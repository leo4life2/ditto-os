#pragma once
#include "backends/soft/a3d_soft_backend.h"
#include "ditto_rig.h"
#include "ditto_face.h"
#include <algorithm>
#include <cmath>

// A plump procedural five-point star: live rotation, round tips and a cream
// highlight. No frame assets or scratch allocation; only its head anchor is
// deformed with the gel. Workers read the same immutable projected state.
struct DittoWinkSparkle {
  float center[3]={},amount=0,phase=0,cx=0,cy=0,iw=0,radius=0,c=1,s=0,tilt=1;
  bool visible=false;
  template<class Deform>
  void pose(bool allowed,float seconds,const float eye[2],Deform deform) {
    visible=false;
    amount=allowed?DittoRig::window(.79f,.86f,1.19f,1.35f,seconds):0;
    if(amount<=.001f)return;
    phase=std::clamp((seconds-.79f)/.56f,0.f,1.f);
    float source[3]={eye[0]-.33f-.035f*phase,eye[1]+.10f+.025f*phase,
                     DittoFace::surface(eye[0],eye[1])+.16f};
    deform(source,center);
  }
  bool project(const float* model,const float* view,int width,int height) {
    visible=false;
    if(amount<=.001f)return false;
    float m[16];a3d::mat4Multiply(view,model,m);
    float w=m[3]*center[0]+m[7]*center[1]+m[11]*center[2]+m[15];
    if(!std::isfinite(w)||w<=.05f)return false;
    iw=1/w;
    cx=((m[0]*center[0]+m[4]*center[1]+m[8]*center[2]+m[12])*iw+1)*width*.5f-.5f;
    cy=((m[1]*center[0]+m[5]*center[1]+m[9]*center[2]+m[13])*iw+1)*height*.5f-.5f;
    radius=.105f*std::hypot(m[0]*width,m[1]*height)*.5f*iw;
    if(!std::isfinite(cx+cy+radius)||radius<=0)return false;
    radius=std::min(radius,24.f)*(.80f+.20f*amount);
    const float angle=-.25f+2.2f*phase;c=std::cos(angle);s=std::sin(angle);
    tilt=.84f+.16f*std::cos(phase*3.14159265f);
    const float extent=radius*1.35f+1;
    visible=cy+extent>=0&&cy-extent<height&&cx+extent>=0&&cx-extent<width;
    return visible;
  }
  static float distance(float x,float y) {
    // Fold into the nearest tip's 72-degree sector, then measure one edge.
    // Broad valleys and an outward offset round the five convex tips. The
    // constants avoid per-pixel trigonometry or a ten-edge polygon search.
    // Mirror symmetry leaves only three possible sectors.
    x=std::abs(x);
    constexpr float axes[3][2]={{0,1},{.951056516f,.309016994f},
      {.587785252f,-.809016994f}};
    float along=y,ax=0,ay=1;
    for(int i=1;i<3;++i) {
      float dot=x*axes[i][0]+y*axes[i][1];
      if(dot>along){along=dot;ax=axes[i][0];ay=axes[i][1];}
    }
    float qx=std::abs(x*ay-y*ax),qy=along-.89f;
    constexpr float ex=.54f*.587785252f,ey=.54f*.809016994f-.89f;
    float t=(qx*ex+qy*ey)/(ex*ex+ey*ey);
    // Interior projections use the signed line distance directly. Only a
    // rounded endpoint needs a square root; the contour stays identical.
    if(t>=0&&t<=1)return (qy*ex-qx*ey)*(1/std::sqrt(ex*ex+ey*ey))-.095f;
    t=std::clamp(t,0.f,1.f);
    float dx=qx-t*ex,dy=qy-t*ey;
    float d=std::sqrt(dx*dx+dy*dy);
    return (qy*ex-qx*ey>=0?d:-d)-.095f;
  }
  static uint16_t blend(uint16_t bg,uint16_t fg,float opacity) {
    int a=int(std::clamp(opacity,0.f,1.f)*256+.5f),b=256-a;
    int r=(((bg>>11)*b+(fg>>11)*a)+128)>>8;
    int g=((int((bg>>5)&63)*b+int((fg>>5)&63)*a)+128)>>8;
    int blue=((int(bg&31)*b+int(fg&31)*a)+128)>>8;
    return uint16_t((r<<11)|(g<<5)|blue);
  }
  void draw(a3d::SoftBackend& renderer,int top,int rows) const {
    const float extent=radius*1.35f+1;
    if(!visible||cy+extent<top||cy-extent>=top+rows)return;
    int left=int(std::floor(cx-extent)),y0=int(std::floor(cy-extent));
    int size=int(std::ceil(extent*2+1));
    float inverse=1/radius,inverse_tilt=1/tilt;
    renderer.drawProjectedPatch(left,y0,size,size,iw,[&](uint16_t bg,int x,int y) {
      float dx=(x-cx)*inverse_tilt*inverse,dy=(y-cy)*inverse;
      if(dx*dx+dy*dy>1.83f)return bg;
      float u=dx*c+dy*s,v=-dx*s+dy*c;
      float d=distance(u,v);
      if(d>=.34f)return bg;
      float halo=std::clamp(1-d/.34f,0.f,1.f);
      float body=std::clamp(.5f-d*radius,0.f,1.f)*amount;
      // A generous creamy center and golden rounded lobes read as one soft
      // solid star, rather than intersecting needles or a thin white outline.
      float light=std::clamp(1-1.45f*((dx+.08f)*(dx+.08f)+(dy+.10f)*(dy+.10f)),0.f,1.f);
      light=light*light*(3-2*light);
      uint16_t color=blend(0xfe89,0xfffc,light);
      return blend(blend(bg,0xff07,halo*halo*amount*.28f),color,body);
    });
  }
};
