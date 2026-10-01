#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Screen/model frame: +X right, +Y up, +Z toward viewer. Specific force in g,
// angular velocity in rad/s. This is a reduced-order fluid/elastic simulation.
namespace BlobMotion {
struct V {
  float x=0,y=0,z=0;
  V operator+(V b) const {return {x+b.x,y+b.y,z+b.z};}
  V operator-(V b) const {return {x-b.x,y-b.y,z-b.z};}
  V operator*(float k) const {return {x*k,y*k,z*k};}
  float length() const {return std::sqrt(x*x+y*y+z*z);}
};
inline V cross(V a,V b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline V cap(V a,float max) {float l=a.length();return l>max?a*(max/l):a;}
inline V normalized(V a) {float l=a.length();return l>.00001f?a*(1/l):V{0,0,1};}
inline bool finite(V a) {return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
inline float dead(float a,float width) {return std::copysign(std::max(0.f,std::abs(a)-width),a);}
inline V dead(V a,float width) {return {dead(a.x,width),dead(a.y,width),dead(a.z,width)};}
// Proper rotation (det=+1). Waveshare portrait gravity demo uses pixel
// displacement (-accelY,+accelX); model Y points up rather than down.
inline V screenAxes(V chip) {return {chip.y,chip.x,-chip.z};}
struct Sample {
  V specific,gyro,gravity,linear,omega,alpha,gyro_bias,shake;
  int64_t time_us=0;
  bool valid=false;
};
struct Filter {
  Sample out;
  V up{0,0,1},bias{},previous_accel{},previous_omega{},quiet_accel{},quiet_gyro{};
  V shake_fast{},shake_slow{};
  float quiet_s=0;
  Sample feed(V a,V w,int64_t now) {
    if(!finite(a)||!finite(w)||a.length()>14||w.length()>22)return {};
    float dt=out.valid?float(now-out.time_us)*1e-6f:0;
    if(!out.valid||dt<=0||dt>.15f) {
      up=normalized(a);previous_accel=a;previous_omega=w-bias;quiet_accel=a;quiet_gyro=w;quiet_s=0;
      shake_fast={};shake_slow={};
      out={};out.specific=a;out.gyro=w;out.gyro_bias=bias;out.gravity=up*-1;
      out.omega=dead(w-bias,.006f);out.time_us=now;out.valid=true;return out;
    }
    dt=std::clamp(dt,.001f,.05f);
    bool quiet=std::abs(a.length()-1)<.08f&&(a-previous_accel).length()<.012f&&w.length()<.25f
        &&(a-quiet_accel).length()<.025f&&(w-quiet_gyro).length()<.018f;
    if(quiet)quiet_s+=dt;else{quiet_s=0;quiet_accel=a;quiet_gyro=w;}
    // This unit has ~5 deg/s zero-rate offset. Requiring a stable acceleration
    // vector for the entire window avoids calibrating a slow pitch/roll away.
    if(quiet_s>1.2f)bias=bias+(w-bias)*(1-std::exp(-dt*2));
    V omega=w-bias;
    // A world-fixed vector in the rotating device frame: du/dt = -w × u.
    float angle=omega.length()*dt;
    if(angle>.000001f) {
      V axis=normalized(omega);float c=std::cos(angle),s=std::sin(angle);
      float dot=axis.x*up.x+axis.y*up.y+axis.z*up.z;
      up=up*c-cross(axis,up)*s+axis*(dot*(1-c));
    }
    // Reject fast acceleration as a gravity correction; gyro carries tilt.
    float trust=std::clamp(1-std::abs(a.length()-1)/.18f,0.f,1.f);
    trust*=std::clamp(1-(a-previous_accel).length()/.10f,0.f,1.f);
    up=normalized(up+(normalized(a)-up)*(trust*(1-std::exp(-dt*.8f))));
    float f=1-std::exp(-dt*30);
    out.specific=a;out.gyro=w;out.gyro_bias=bias;out.gravity=up*-1;
    out.linear=out.linear+(cap(a-up,3.f)-out.linear)*f;
    // A separate band-pass keeps rapid reversals out of the slow slosh path.
    // Never feed it raw gravity or alter the established gravity/gyro filters.
    shake_fast=shake_fast+(cap(a-up,6.f)-shake_fast)*(1-std::exp(-dt*70));
    shake_slow=shake_slow+(shake_fast-shake_slow)*(1-std::exp(-dt*7));
    out.shake=cap(dead(shake_fast-shake_slow,.020f),4.f);
    out.omega=out.omega+(dead(omega,.006f)-out.omega)*f;
    V derivative=cap((omega-previous_omega)*(1/dt),35.f);
    out.alpha=out.alpha+(derivative-out.alpha)*(1-std::exp(-dt*12));
    out.time_us=now;out.valid=true;previous_accel=a;previous_omega=omega;return out;
  }
};
struct Mode {
  float p=0,v=0;
  void step(float dt,float target,float stiffness,float damping,float limit) {
    v+=((target-p)*stiffness-damping*v)*dt;p+=v*dt;
    if(std::abs(p)>limit){p=std::copysign(limit,p);if(p*v>0)v*= -.65f;}
  }
};
// Qualify rapid alternating sensor input, not a single pickup or a slow swing.
// Cessation follows fresh input, so residual mesh oscillation cannot re-arm it.
struct ShakeRecovery {
  bool shaking=false,recovering=false;
  float time=0,last_drive=-10,last_peak=-10,quiet=0,ramp=0;
  V peak;
  int reversals=0;
  float scale=1;
  void cancel(){*this=ShakeRecovery{};}
  float step(float dt,const Sample& s) {
    time+=dt;
    if(!s.valid){cancel();return 1;}
    float amplitude=s.shake.length();
    // Once the shake ends, even a gentle renewed swing or tilt must restore
    // the original liquid response. These thresholds remain above rest noise.
    bool drive=amplitude>.055f || s.linear.length()>.065f || s.omega.length()>.12f;
    if(amplitude>.11f) {
      V direction=normalized(s.shake);
      float dot=direction.x*peak.x+direction.y*peak.y+direction.z*peak.z;
      if(last_peak<0){last_peak=time;peak=direction;}
      else if(dot<-.45f && time-last_peak>.055f) {
        float interval=time-last_peak;
        reversals=interval<.30f?reversals+1:0;
        last_peak=time;peak=direction;
        if(reversals>=2)shaking=true;
      }
    }
    if(time-last_peak>.45f){reversals=0;last_peak=-10;}
    if(drive) {
      last_drive=time;quiet=0;
      // Resume the original loose coefficients immediately on renewed input.
      recovering=false;ramp=0;scale=1;
    } else quiet+=dt;
    if(shaking && quiet>.12f) {shaking=false;recovering=true;ramp=0;}
    if(recovering) {
      ramp=std::min(.12f,ramp+dt);float f=ramp/.12f;f=f*f*(3-2*f);scale=1+2.5f*f;
    }
    return scale;
  }
};
struct Liquid {
  Mode x,y,z,bend_x,bend_y,bend_z,spin_x,spin_y,spin_z,depth;
  Mode wave_x,wave_y,wave_z;
  float lateral=1,thickness=1,normal_lateral=1,normal_thickness=1,excitement=0;
  ShakeRecovery recovery;
  float damping_scale=1;
  void step(float dt,const Sample &sample) {
    damping_scale=recovery.step(dt,sample);
    V gravity=sample.valid?sample.gravity:V{},linear=sample.valid?dead(sample.linear,.012f):V{};
    V omega=sample.valid?sample.omega:V{},alpha=sample.valid?sample.alpha:V{};
    V shake=sample.valid?sample.shake:V{};
    float energy=std::clamp(linear.length()*.65f+omega.length()*.13f,0.f,1.f);
    excitement+=(energy-excitement)*(1-std::exp(-dt*8));
    int steps=std::max(1,int(std::ceil(dt*120)));float h=dt/steps;
    for(int i=0;i<steps;i++) {
      // Requested feel: reverse only the gravity-separated linear force.
      // Gravity, sensor fusion and gyro torque keep their original signs.
      // Lower restoring stiffness and damping let the mass flow and coast.
      // More sensitive input gains; keep the accepted jiggle damping and bounds.
      x.step(h,gravity.x*.18f+linear.x*.75f,34,1.8f*damping_scale,.20f);
      y.step(h,gravity.y*.14f+linear.y*.57f,39,1.9f*damping_scale,.16f);
      z.step(h,linear.z*.30f,42,2.0f*damping_scale,.12f);
      bend_x.step(h,gravity.x*.20f+linear.x*.68f-x.v*.040f,18,1.35f*damping_scale,.36f);
      bend_y.step(h,gravity.y*.11f+linear.y*.44f-y.v*.040f,21,1.45f*damping_scale,.26f);
      bend_z.step(h,linear.z*.40f-z.v*.040f,20,1.45f*damping_scale,.28f);
      // Counter-rotation and angular-acceleration lag; no yaw integration drift.
      spin_x.step(h,-omega.x*.12f-alpha.x*.011f,43,1.8f*damping_scale,.20f);
      spin_y.step(h,-omega.y*.12f-alpha.y*.011f,43,1.8f*damping_scale,.20f);
      spin_z.step(h,-omega.z*.18f-alpha.z*.014f,38,1.6f*damping_scale,.24f);
      depth.step(h,gravity.z*.025f+linear.z*.28f,39,1.8f*damping_scale,.24f);
      // ~3 Hz surface modes: faster than the <1 Hz mass modes above.
      // Positive drive retains the user-selected acceleration direction.
      wave_x.step(h,shake.x*.44f,280,1.7f*damping_scale,.44f);
      wave_y.step(h,shake.y*.36f,340,1.9f*damping_scale,.36f);
      wave_z.step(h,shake.z*.44f,300,1.8f*damping_scale,.40f);
    }
    thickness=1+depth.p;normal_lateral=std::sqrt(thickness);lateral=1/normal_lateral;normal_thickness=1/thickness;
  }
  float activity() const {
    return std::abs(x.p)+std::abs(y.p)+std::abs(z.p)+std::abs(bend_x.p)+std::abs(bend_y.p)+std::abs(bend_z.p)+std::abs(spin_x.p)+std::abs(spin_y.p)+std::abs(spin_z.p)+std::abs(depth.p)+std::abs(wave_x.p)+std::abs(wave_y.p)+std::abs(wave_z.p);
  }
  void deform(float *p,float *n) const {
    // Each shear and reciprocal depth scaling has determinant one: preserve
    // volume. Apply its exact inverse-transpose to the lighting normal.
    // Bulk material supplies gravity-directed support; depth slosh is centered.
    p[0]*=lateral;p[1]*=lateral;p[2]*=thickness;
    n[0]*=normal_lateral;n[1]*=normal_lateral;n[2]*=normal_thickness;
    float height=p[1]+.8f;
    p[0]+=bend_x.p*height*height*.55f;n[1]-=bend_x.p*height*1.1f*n[0];
    p[1]+=bend_y.p*(p[0]*p[0]-.35f)*.8f;n[0]-=bend_y.p*p[0]*1.6f*n[1];
    height=p[1]+.8f;
    p[2]+=bend_z.p*height*height*.45f;n[1]-=bend_z.p*height*.9f*n[2];
    // Opposed layers give a jelly ripple instead of shaking one rigid mass.
    // These are also unit-determinant shears, with exact normal transforms.
    float layer=p[1]+.35f;
    p[0]+=wave_x.p*(layer*layer-.4f);n[1]-=wave_x.p*2*layer*n[0];
    p[1]+=wave_y.p*(p[0]*p[0]-.35f);n[0]-=wave_y.p*2*p[0]*n[1];
    layer=p[1]+.35f;
    p[2]+=wave_z.p*(layer*layer-.4f);n[1]-=wave_z.p*2*layer*n[2];
    p[0]+=spin_y.p*p[2];n[2]-=spin_y.p*n[0];
    p[1]-=spin_x.p*p[2];n[2]+=spin_x.p*n[1];
    p[0]-=spin_z.p*(p[1]+.55f);n[1]+=spin_z.p*n[0];
    p[0]+=x.p;p[1]+=y.p;p[2]+=z.p;
  }
};
}
