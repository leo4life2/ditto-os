#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "blob_motion.h"
#include "blob_material.h"

namespace Blob {
inline float inverse_length(float value) {
  uint32_t bits; __builtin_memcpy(&bits,&value,sizeof(bits));
  bits=0x5f3759dfu-(bits>>1);
  float n; __builtin_memcpy(&n,&bits,sizeof(n));
  n*=1.5f-.5f*value*n*n;
  return n*(1.5f-.5f*value*n*n);
}
struct Touch {
  bool down=false;
  int x=205,y=290,press_x=205,press_y=290;
  uint32_t press=0;
};
struct Spring {
  float p=0,v=0;
  void follow(float dt,float target) {
    // Exact critically damped contact follower (50% in 21 ms). Only held skin
    // uses this; free release and all bulk material modes retain their springs.
    constexpr float omega=80;
    float error=p-target,c=v+omega*error,decay=std::exp(-omega*dt);
    p=target+(error+c*dt)*decay;v=(v-omega*c*dt)*decay;
  }
  void step(float dt,float target,float stiffness,float damping,float limit,float restitution=.15f) {
    v+=((target-p)*stiffness-v*damping)*dt;
    p+=v*dt;
    if(std::abs(p)>limit) {p=std::copysign(limit,p);v*= -restitution;}
  }
};
// A local elastic handle plus delayed slosh modes. Geometry is always rebuilt
// from rest, with bounded forces and <=1/120 s integration steps.
struct Physics {
  BlobMotion::Liquid liquid;
  Material material;
  Spring poke,ripple,x,y,roll,yaw,squash,dent,pull_x,pull_y,slosh_x,slosh_y,bounce_x,bounce_y,bounce_z,bounce_roll;
  float anchor_x=0,anchor_y=0,last_x=0,last_y=0;
  float dent_x=0,dent_y=0,annoyance=0,attention=0;
  float dent_target_x=0,dent_target_y=0;
  float held_s=0,xx_timer=0,side_timer=0,x_eyes=0,side_eye=0,gaze=0,surprise=0;
  bool grabbed=false,was_down=false;
  uint32_t seen_press=0,impact_serial=0,tap_count=0;
  bool impact_hit=false;
  float impact_x=0,impact_y=0,impact_nx=0,impact_ny=0,impact_z=.5f,impact_nz=1,max_travel2=0;
  float poke_center[3]={0,0,.5f},poke_direction[3]={0,0,-1};
  void setImpact(uint32_t serial,bool hit,float px=0,float py=0,float nx=0,float ny=0,float pz=.5f,float nz=1) {
    impact_serial=serial;impact_hit=hit;impact_x=px;impact_y=py;impact_nx=nx;impact_ny=ny;impact_z=pz;impact_nz=nz;
  }
  void reset() {auto id=seen_press,count=tap_count;*this=Physics{};seen_press=id;tap_count=count;}
  void step(float dt,const Touch &t,bool asleep=false,const BlobMotion::Sample &motion={},const BlobMotion::V *body_gravity=nullptr) {
    dt=std::clamp(dt,0.f,.1f);
    if(asleep) {reset();seen_press=t.press;return;}
    if(t.press!=seen_press)liquid.recovery.cancel();
    liquid.step(dt,motion);
    float fx=(t.x-205)/145.f,fy=(290-t.y)/145.f;
    const bool new_press=t.press!=seen_press;
    if(new_press) {
      seen_press=t.press;
      anchor_x=(t.press_x-205)/145.f;anchor_y=(290-t.press_y)/145.f;
      grabbed=anchor_x*anchor_x/1.44f+anchor_y*anchor_y/.95f<1.1f;
      if(impact_serial==t.press)grabbed=impact_hit;
      else {impact_hit=grabbed;impact_x=anchor_x;impact_y=anchor_y;impact_nx=anchor_x*.7f;impact_ny=anchor_y*.7f;impact_z=.5f;impact_nz=1;}
      last_x=fx;last_y=fy;held_s=0;
      max_travel2=(fx-anchor_x)*(fx-anchor_x)+(fy-anchor_y)*(fy-anchor_y);
      if(grabbed) {
        dent_target_x=impact_x;
        dent_target_y=impact_y;
        // One rounded impact on touchdown: local dent plus whole-body momentum.
        // A continuing hold/drag does not keep injecting impact energy.
        poke_center[0]=impact_x;poke_center[1]=impact_y;poke_center[2]=impact_z;
        const float inv=1/std::sqrt(impact_nx*impact_nx+impact_ny*impact_ny+impact_nz*impact_nz+1e-8f);
        poke_direction[0]=-impact_nx*inv;poke_direction[1]=-impact_ny*inv;poke_direction[2]=-impact_nz*inv;
        poke.p=0;poke.v=5.5f;
        bounce_x.v=std::clamp(bounce_x.v+poke_direction[0]*1.4f,-2.2f,2.2f);
        bounce_y.v=std::clamp(bounce_y.v+poke_direction[1]*1.4f,-2.2f,2.2f);
        bounce_z.v=std::clamp(bounce_z.v+poke_direction[2]*1.2f,-1.8f,1.8f);
        float torque=(impact_x*poke_direction[1]-(impact_y+.2f)*poke_direction[0])*.8f;
        bounce_roll.v=std::clamp(bounce_roll.v+torque,-1.4f,1.4f);
        // The same contact excites the body waves, not only the dented skin.
        liquid.wave_x.v=std::clamp(liquid.wave_x.v+poke_direction[0]*1.8f,-4.f,4.f);
        liquid.wave_y.v=std::clamp(liquid.wave_y.v+poke_direction[1]*1.8f,-4.f,4.f);
        liquid.wave_z.v=std::clamp(liquid.wave_z.v+poke_direction[2]*1.4f,-4.f,4.f);
        // Contact compresses the bulk along its surface normal and sends
        // volume into the other axes, through the same yielding material.
        material.impulse(-.65f*poke_direction[0]*poke_direction[0],
                         -.65f*poke_direction[1]*poke_direction[1],
                         -.65f*poke_direction[2]*poke_direction[2]);
        // Shift the dent center gradually below; an interrupted pull remains
        // continuous instead of snapping to a new finger location.
        annoyance=std::min(1.f,annoyance+.18f);attention=1;
      }
    }
    float tx=0,ty=0,travel=0;
    if(grabbed&&t.down) {
      held_s+=dt;
      max_travel2=std::max(max_travel2,(fx-anchor_x)*(fx-anchor_x)+(fy-anchor_y)*(fy-anchor_y));
      float dx=fx-last_x,dy=fy-last_y;travel=std::abs(dx)+std::abs(dy);
      tx=(fx-anchor_x)*.86f;ty=(fy-anchor_y)*.86f;
      const float length=std::sqrt(tx*tx+ty*ty);
      if(length>.66f) {tx*=.66f/length;ty*=.66f/length;}
      float turn=std::clamp(last_x*dy-last_y*dx,-.25f,.25f);
      roll.v+=turn*.7f;yaw.v+=std::clamp(dx,-.25f,.25f)*.35f;
      slosh_x.v-=dx*.48f;slosh_y.v-=dy*.32f;
      // Fast finger movement sends a counter-wave through the trailing body.
      liquid.wave_x.v=std::clamp(liquid.wave_x.v-std::clamp(dx,-.25f,.25f)*1.8f,-4.f,4.f);
      liquid.wave_y.v=std::clamp(liquid.wave_y.v-std::clamp(dy,-.25f,.25f)*1.2f,-4.f,4.f);
      annoyance=std::min(1.f,annoyance+travel*.09f);
      last_x=fx;last_y=fy;attention=1;
    }
    if(!t.down && grabbed && (was_down||new_press) && held_s<.24f && max_travel2<.0081f) {
      // Classify the tap, but never add a second impact on release.
      dent_x=dent_target_x=impact_x;dent_y=dent_target_y=impact_y;
      tap_count++;
    }
    if(!t.down) {grabbed=false;held_s=0;}
    was_down=t.down;
    const bool held=grabbed&&t.down;
    side_timer=held&&max_travel2>.0081f?1.25f:std::max(0.f,side_timer-dt);
    // Stationary contact stays local throughout the tap window. Moving a
    // grab enables the existing broad drag response immediately; a held poke
    // gradually becomes the familiar squish after the tap window expires.
    const float pressure=held?(max_travel2>=.0081f?1.f:std::clamp((held_s-.24f)/.12f,0.f,1.f)):0;
    const float squeeze=pressure*std::clamp(-.065f+ty*.30f,-.24f,.10f);
    int steps=std::max(1,int(std::ceil(dt*120.f)));float h=dt/steps;
    for(int i=0;i<steps;i++) {
      poke.step(h,0,145,4,.31f,.50f);
      // Delayed outward ring carries the initial indentation into nearby skin.
      ripple.step(h,-poke.p*.85f,70,2.4f,.17f,.50f);
      x.step(h,tx*.055f,60,9,.08f);y.step(h,ty*.035f,65,9,.05f);
      roll.step(h,-tx*.055f,48,7,.13f);yaw.step(h,tx*.08f,40,7,.13f);
      if(held) {pull_x.follow(h,tx);pull_y.follow(h,ty);}
      else {
        pull_x.step(h,0,95.f,2.7f,.78f,.35f);
        pull_y.step(h,0,95.f,2.7f,.78f,.35f);
      }
      // The length cap also bounds the deformation Jacobian away from folds.
      float l2=pull_x.p*pull_x.p+pull_y.p*pull_y.p;
      if(l2>.78f*.78f) {float inv=.78f/std::sqrt(l2);pull_x.p*=inv;pull_y.p*=inv;pull_x.v*=.85f;pull_y.v*=.85f;}
      squash.step(h,squeeze,held?95.f:70.f,held?8.f:3.f,.25f);
      dent.step(h,pressure*(.23f+std::min(.09f,held_s*.10f)),held?115.f:70.f,held?11.f:3.2f,.37f);
      bounce_x.step(h,0,42,2.3f,.22f,.5f);bounce_y.step(h,0,46,2.4f,.20f,.5f);
      bounce_z.step(h,0,48,2.5f,.18f,.5f);
      bounce_roll.step(h,0,40,2.2f,.22f,.5f);
      slosh_x.step(h,-pull_x.p*.42f,20,1.9f,.29f);
      slosh_y.step(h,-pull_y.p*.24f,26,2.1f,.21f);
      const float follow=std::min(1.f,h*18);
      dent_x+=(dent_target_x-dent_x)*follow;
      dent_y+=(dent_target_y-dent_y)*follow;
    }
    // Weight follows fused world gravity, expressed in the deforming body's
    // frame. Keep the accepted touch and linear-acceleration forces intact.
    const auto gravity=motion.valid?(body_gravity?*body_gravity:motion.gravity):BlobMotion::V{};
    auto acceleration=motion.valid?BlobMotion::dead(motion.linear,.012f):BlobMotion::V{};
    material.step(dt,2.5f*std::abs(tx)-.7f*std::abs(acceleration.x),
                  3.f*ty+.75f*acceleration.y,
                  -.8f*pressure-.6f*std::abs(acceleration.z),gravity);
    const float stretch=std::sqrt(pull_x.p*pull_x.p+pull_y.p*pull_y.p);
    if((held&&(stretch>.37f || held_s>.75f))||liquid.excitement>.85f)xx_timer=.45f;
    xx_timer=std::max(0.f,xx_timer-dt);
    const float f=1-std::exp(-dt*12);
    x_eyes+=((xx_timer>0?1.f:0.f)-x_eyes)*f;
    side_eye+=((side_timer>0&&xx_timer<=0 ? 1.f:0.f)-side_eye)*(1-std::exp(-dt*7));
    gaze+=(std::clamp(last_x,-1.f,1.f)-gaze)*f;
    surprise+=(std::max(held?std::clamp(stretch*2,0.f,1.f):0.f,liquid.excitement*.7f)-surprise)*f;
    annoyance=std::max(0.f,annoyance-dt*.075f);
    attention=std::max(0.f,attention-dt*.55f);
  }
  float stretch() const {return std::sqrt(pull_x.p*pull_x.p+pull_y.p*pull_y.p);}
  float activity() const {return material.activity()+liquid.activity()+std::abs(poke.p)+std::abs(ripple.p)+std::abs(x.p)+std::abs(y.p)+std::abs(roll.p)+std::abs(yaw.p)+std::abs(squash.p)+std::abs(dent.p)+stretch()+std::abs(slosh_x.p)+std::abs(slosh_y.p)+std::abs(bounce_x.p)+std::abs(bounce_y.p)+std::abs(bounce_z.p)+std::abs(bounce_roll.p);}
  // One broad material-space handle pulls the skin locally. The analytic
  // inverse-transpose keeps highlights attached to its changing shape.
  void deform(const float rest[3],const float base_normal[3],float out[3],float normal[3]) const {
    float p[3]={rest[0],rest[1],rest[2]},n[3]={base_normal[0],base_normal[1],base_normal[2]};
    if(std::abs(poke.p)>.0001f || std::abs(ripple.p)>.0001f) {
      // Rounded ~47 px half-strength footprint. The outer-minus-core kernel
      // makes an annular ripple which follows the dent with spring inertia.
      float delta[3],distance2=0;
      for(int k=0;k<3;k++){delta[k]=p[k]-poke_center[k];distance2+=delta[k]*delta[k];}
      const float inv=1/(1+4.f*distance2),weight=inv*inv;
      const float outer=1/(1+distance2),outer_weight=outer*outer;
      const float amount=poke.p*weight+ripple.p*(outer_weight-weight);
      float gradient[3],denominator=1,projection=0;
      for(int k=0;k<3;k++) {
        const float core_gradient=-16.f*delta[k]*weight*inv;
        const float outer_gradient=-4.f*delta[k]*outer_weight*outer;
        gradient[k]=poke.p*core_gradient+ripple.p*(outer_gradient-core_gradient);
        denominator+=gradient[k]*poke_direction[k];projection+=n[k]*poke_direction[k];
        p[k]+=poke_direction[k]*amount;
      }
      // Inverse transpose of I + direction * gradient^T. The bounded
      // amplitude keeps its determinant positive, including rebound.
      for(int k=0;k<3;k++)n[k]-=gradient[k]*projection/denominator;
    }
    float dx=p[0]-dent_x,dy=p[1]-dent_y;
    float inv=1/(1+1.15f*(dx*dx+dy*dy)),w=inv*inv;
    float wx=-4.6f*dx*w*inv,wy=-4.6f*dy*w*inv;
    float a=std::clamp(dent.p,-.10f,.37f),zz=1-a*w;
    out[0]=p[0]+pull_x.p*w;out[1]=p[1]+pull_y.p*w;out[2]=p[2]*zz;
    normal[2]=n[2]/zz;
    float nx=n[0]+a*p[2]*wx*normal[2],ny=n[1]+a*p[2]*wy*normal[2];
    float factor=(pull_x.p*nx+pull_y.p*ny)/(1+pull_x.p*wx+pull_y.p*wy);
    normal[0]=nx-wx*factor;normal[1]=ny-wy*factor;
    // Delayed cross-body waves continue after the finger lets go.
    float height=out[1]+.85f;
    out[0]+=slosh_x.p*height*height*.55f;
    normal[1]-=slosh_x.p*height*1.1f*normal[0];
    float slope=slosh_y.p*out[0]*1.6f;
    out[1]+=slosh_y.p*(out[0]*out[0]-.35f)*.8f;
    normal[0]-=slope*normal[1];
    liquid.deform(out,normal);
    material.deform(out,normal);
    float invlen=inverse_length(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
    for(int k=0;k<3;k++)normal[k]*=invlen;
  }
};
}
