#pragma once
#include "avatar_asset.h"
#include <algorithm>
#include <cmath>

// A small skeletal rig for a continuous gel surface. Bind weights and their
// derivatives are cached on body vertices; changing facial geometry samples
// the same field. No detached hands, duplicated skin, or animation frames.
namespace DittoRig {
enum Bone { Root, Body, Head, LeftHand, RightHand, Count };
struct Joint {const char* name;int parent;float pivot[3];};
inline constexpr Joint joints[Count]={
  {"root",-1,{0,-.82f,0}}, {"body",Root,{0,-.48f,0}},
  {"head",Body,{0,.10f,0}}, {"left hand",Body,{-.58f,.26f,0}},
  {"right hand",Body,{.60f,.03f,0}}};
struct Control {float rx=0,ry=0,rz=0,x=0,y=0,z=0;};
struct Pose {Control bone[Count];float gaze_x=0,gaze_y=0,open=0,wink=0,squiggle=0;};
struct Affine {
  float r[9]={1,0,0,0,1,0,0,0,1},t[3]={};
  void point(const float* p,float* out) const {
    for(int i=0;i<3;i++)out[i]=r[i*3]*p[0]+r[i*3+1]*p[1]+r[i*3+2]*p[2]+t[i];
  }
};
inline Affine compose(const Affine& a,const Affine& b) {
  Affine c;
  for(int i=0;i<3;i++)for(int j=0;j<3;j++) {
    c.r[i*3+j]=0;for(int k=0;k<3;k++)c.r[i*3+j]+=a.r[i*3+k]*b.r[k*3+j];
  }
  a.point(b.t,c.t);return c;
}
inline Affine transform(Control c,const float* pivot) {
  float cx=std::cos(c.rx),sx=std::sin(c.rx),cy=std::cos(c.ry),sy=std::sin(c.ry),cz=std::cos(c.rz),sz=std::sin(c.rz);
  Affine a; // Rz Ry Rx, with an explicit rest-space joint pivot.
  float r[]={cz*cy,cz*sy*sx-sz*cx,cz*sy*cx+sz*sx,
             sz*cy,sz*sy*sx+cz*cx,sz*sy*cx-cz*sx,-sy,cy*sx,cy*cx};
  std::copy(r,r+9,a.r);float tr[]={c.x,c.y,c.z};
  for(int i=0;i<3;i++)a.t[i]=pivot[i]+tr[i]-r[i*3]*pivot[0]-r[i*3+1]*pivot[1]-r[i*3+2]*pivot[2];
  return a;
}
inline float smooth(float a,float b,float x) {float u=std::clamp((x-a)/(b-a),0.f,1.f);return u*u*(3-2*u);}
inline float derivative(float a,float b,float x) {float u=std::clamp((x-a)/(b-a),0.f,1.f);return 6*u*(1-u)/(b-a);}
inline float window(float a,float b,float c,float d,float t) {return smooth(a,b,t)*(1-smooth(c,d,t));}
struct Binding {float w[3],dx[3],dy[3];};
inline Binding bind(const float* p) {
  float l=smooth(.32f,1.10f,-p[0]),ly=smooth(-.18f,.25f,p[1]),lt=1-smooth(.40f,.90f,p[1]);
  float r=smooth(.32f,1.10f,p[0]),ry=smooth(-.53f,-.04f,p[1]),rt=1-smooth(.14f,.65f,p[1]);
  float left=l*ly*lt,right=r*ry*rt,head=smooth(-.18f,.40f,p[1]);
  float lx=-derivative(.32f,1.10f,-p[0])*ly*lt,ldy=l*(derivative(-.18f,.25f,p[1])*lt-ly*derivative(.40f,.90f,p[1]));
  float rx=derivative(.32f,1.10f,p[0])*ry*rt,rdy=r*(derivative(-.53f,-.04f,p[1])*rt-ry*derivative(.14f,.65f,p[1]));
  return {{head*(1-left-right),left,right},
          {-head*(lx+rx),lx,rx},
          {derivative(-.18f,.40f,p[1])*(1-left-right)-head*(ldy+rdy),ldy,rdy}};
}
// Every gesture anticipates, acts, then returns to the bind pose before the
// four-second action ends. Conversations are gentle repeating poses instead.
inline Pose sample(int clip,float t) {
  Pose p;auto& b=p.bone[Body];auto& h=p.bone[Head];auto& l=p.bone[LeftHand];auto& r=p.bone[RightHand];
  constexpr float pi=3.14159265f;
  float e=window(.10f,.65f,2.90f,3.75f,t),a=window(0,.18f,.26f,.48f,t);
  float beat=std::sin((t-.65f)*2*pi*1.4f),pulse=std::sin((t-.55f)*2*pi*2.2f);
  switch(clip) {
    case Avatar::Wave:
      r.rz=e*(.35f+.11f*beat)-.10f*a;r.y=.035f*e;r.x=.10f*e;
      h.rz=-.11f*e;h.ry=.10f*e;l.rz=.05f*e;b.rz=-.035f*e;break;
    case Avatar::Yes:
      h.rx=-.26f*std::sin((t-.35f)*2*pi*1.25f)*e;h.y=-.025f*e;
      l.rz=-.055f*e;r.rz=.055f*e;break;
    case Avatar::No:
      p.squiggle=e;
      h.ry=.32f*std::sin((t-.35f)*2*pi*1.15f)*e;h.rz=.025f*e;
      p.gaze_x=.026f*std::sin((t-.35f)*2*pi*1.15f)*e;break;
    case Avatar::Curious:
      h.rz=-.23f*e;h.ry=.10f*e;h.y=.035f*e;r.rz=.20f*e;l.rz=.045f*e;
      p.gaze_x=.042f*e;p.gaze_y=.024f*e;break;
    case Avatar::Point:
      r.x=.10f*e;r.y=.04f*e;r.rz=.25f*e;b.x=-.025f*e;h.ry=.21f*e;h.rz=-.07f*e;
      p.gaze_x=.065f*e;b.rz=-.035f*e;break;
    case Avatar::Celebrate:
      r.rz=(.34f+.07f*pulse)*e;l.rz=-(.32f+.07f*pulse)*e;
      r.y=l.y=.04f*e;h.y=.035f*e;b.y=.045f*e*(.5f+.5f*beat);h.rz=.05f*beat*e;
      p.open=.45f*e;break;
    case Avatar::Sad:
      p.squiggle=e;
      h.rx=.15f*e;h.y=-.075f*e;h.rz=.08f*e;r.rz=-.22f*e;l.rz=.18f*e;
      p.gaze_y=-.050f*e;b.y=-.02f*e;break;
    case Avatar::Giggle:
      h.rz=.045f*pulse*e;h.y=.018f*pulse*e;b.y=.018f*std::sin(t*2*pi*3)*e;
      l.rz=-.11f*e;r.rz=.20f*e;r.x=-.04f*e;p.open=.22f*e;break;
    case Avatar::Happy:
      h.rz=.075f*beat*e;r.rz=.25f*e;l.rz=-.23f*e;b.y=.025f*e;p.open=.28f*e;break;
    case Avatar::Surprise:
      h.y=.08f*e;h.rx=-.12f*e;r.rz=.34f*e;l.rz=-.30f*e;r.x=.04f*e;l.x=-.04f*e;
      p.open=.85f*e;break;
    case Avatar::Wink:
      h.rz=-.10f*e;r.rz=.24f*e;p.wink=window(.750f,.790f,1.190f,1.265f,t);break;
    case Avatar::Listening:
      h.rz=-.10f;h.rx=-.07f+.012f*std::sin(t*3.8f);
      h.y=.025f+.007f*std::sin(t*3.8f);r.rz=.10f+.018f*std::sin(t*3.8f);p.gaze_y=.018f;break;
    case Avatar::Thinking:
      h.rz=.13f;h.ry=.06f*std::sin(t*1.1f);l.rz=-.15f;l.x=.035f;p.gaze_x=-.040f;p.gaze_y=.028f;break;
    case Avatar::Speaking:
      h.rz=.030f*std::sin(t*3);r.rz=.13f+.045f*std::sin(t*4);l.rz=-.05f;break;
    default:break;
  }
  return p;
}
struct Skeleton {
  Pose pose;
  Affine palette[Count];
  bool active=false;
  void step(float dt,int clip,float seconds,bool allowed) {
    Pose target=allowed?sample(clip,seconds):Pose{};
    float f=1-std::exp(-std::clamp(dt,0.f,.1f)*24.f),peak=0;
    auto approach=[&](float& value,float to){value+=(to-value)*f;if(std::abs(value)<1e-6f&&to==0)value=0;peak=std::max(peak,std::abs(value));};
    for(int i=0;i<Count;i++) {
      auto& q=pose.bone[i];auto& t=target.bone[i];
      approach(q.rx,t.rx);approach(q.ry,t.ry);approach(q.rz,t.rz);
      approach(q.x,t.x);approach(q.y,t.y);approach(q.z,t.z);
    }
    approach(pose.gaze_x,target.gaze_x);approach(pose.gaze_y,target.gaze_y);
    approach(pose.open,target.open);approach(pose.squiggle,target.squiggle);
    // Eyelids follow their 515 ms close/hold/open schedule directly. The
    // slower body-pose follower would stretch this into an expanding morph.
    pose.wink=target.wink;peak=std::max(peak,std::abs(pose.wink));
    active=peak>0;
    if(!active)return;
    for(int i=0;i<Count;i++) {
      auto local=transform(pose.bone[i],joints[i].pivot);
      palette[i]=joints[i].parent<0?local:compose(palette[joints[i].parent],local);
    }
  }
  // Exact Jacobian of the weighted affine field, including weight gradients.
  // Its inverse-transpose keeps the glossy shoulder/neck shading continuous.
  float deform(const float* p,const float* n,float* out,float* normal,const Binding& weights) const {
    if(!active){std::copy(p,p+3,out);std::copy(n,n+3,normal);return 1;}
    float base[3],q[3],j[9];palette[Body].point(p,base);std::copy(base,base+3,out);
    std::copy(palette[Body].r,palette[Body].r+9,j);
    for(int bone=Head;bone<=RightHand;bone++) {
      int k=bone-Head;
      // No contribution to either the position or its Jacobian. Preserve the
      // equations/order for every nonzero influence and all liquid physics.
      if(weights.w[k]==0 && weights.dx[k]==0 && weights.dy[k]==0)continue;
      palette[bone].point(p,q);
      for(int row=0;row<3;row++) {
        float delta=q[row]-base[row];out[row]+=weights.w[k]*delta;
        for(int col=0;col<3;col++)j[row*3+col]+=weights.w[k]*(palette[bone].r[row*3+col]-palette[Body].r[row*3+col]);
        j[row*3]+=weights.dx[k]*delta;j[row*3+1]+=weights.dy[k]*delta;
      }
    }
    float c[]={j[4]*j[8]-j[5]*j[7],j[5]*j[6]-j[3]*j[8],j[3]*j[7]-j[4]*j[6],
               j[2]*j[7]-j[1]*j[8],j[0]*j[8]-j[2]*j[6],j[1]*j[6]-j[0]*j[7],
               j[1]*j[5]-j[2]*j[4],j[2]*j[3]-j[0]*j[5],j[0]*j[4]-j[1]*j[3]};
    float det=j[0]*c[0]+j[1]*c[1]+j[2]*c[2];
    for(int row=0;row<3;row++)normal[row]=(c[row*3]*n[0]+c[row*3+1]*n[1]+c[row*3+2]*n[2])/det;
    return det;
  }
  float deform(const float* p,const float* n,float* out,float* normal) const {return deform(p,n,out,normal,bind(p));}
};
}
