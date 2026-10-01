#include "blob_physics.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace BlobMotion;
static float dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static Sample stationary(V a={0,0,1}){Filter f;return f.feed(a,{},10000);}
int main(){
 V a{1,2,3},b{-2,3,1};assert((cross(screenAxes(a),screenAxes(b))-screenAxes(cross(a,b))).length()<.00001f);
 assert(screenAxes({1,0,0}).y==1 && screenAxes({0,1,0}).x==1 && screenAxes({0,0,1}).z==-1);
 Filter f;int64_t now=1000000;
 for(int i=0;i<600;i++){now+=16667;f.feed({0,0,1},{.015f,.095f,.004f},now);}
 assert(f.out.linear.length()<.006f && f.out.omega.length()<.005f);
 // Known 90 degree rotation around Y: rotate gravity with gyro rather than
 // misinterpreting the changing accelerometer direction as a translation.
 Filter turn;now=1000000;turn.feed({0,0,1},{},now);
 for(int i=1;i<=120;i++){float t=i/120.f;now+=8333;turn.feed({-std::sin(t*1.5707963f),0,std::cos(t*1.5707963f)},{0,1.5707963f,0},now);}
 assert(turn.out.gravity.x>.995f && turn.out.linear.length()<.012f);
 // Translational impulse follows device acceleration (requested inversion) and leaves residual
 // momentum after input stops; six axes independently reach shape modes.
 for(int axis=0;axis<3;axis++) {
  Filter impulse;Liquid liquid;now=1000000;impulse.feed({0,0,1},{},now);
  for(int i=0;i<12;i++) {
   V acc{0,0,1};if(axis==0)acc.x=1.2f;if(axis==1)acc.y=1.2f;if(axis==2)acc.z=2.2f;
   now+=16667;liquid.step(1.f/60,impulse.feed(acc,{},now));
  }
  assert(impulse.out.gravity.z<-.99f);
  float p=axis==0?liquid.x.p:axis==1?liquid.y.p:liquid.z.p;assert(p>.035f);
  float before=liquid.activity();liquid.step(1.f/60,stationary());assert(liquid.activity()>.5f*before);
  Liquid spin;Sample s=stationary();if(axis==0)s.omega.x=3;if(axis==1)s.omega.y=3;if(axis==2)s.omega.z=3;
  for(int i=0;i<20;i++)spin.step(1.f/60,s);
  p=axis==0?spin.spin_x.p:axis==1?spin.spin_y.p:spin.spin_z.p;assert(p<-.05f);
 }
 for(int sign:{-1,1}){Liquid l;for(int i=0;i<200;i++)l.step(1.f/60,stationary({float(-sign),0,0}));assert(l.x.p*sign>.09f && l.bend_x.p*sign>.07f);}
 // Invalid input never poisons subsequent samples.
 assert(!f.feed({NAN,0,1},{},now+10000).valid);assert(finite(f.feed({0,0,1},{},now+20000).gravity));
 // Device motion transformations preserve volume, and shading normals stay
 // perpendicular to both finite-difference surface tangents.
  Liquid l;Sample extreme=stationary({.5f,.3f,.8f});extreme.linear={1,-1,1};extreme.omega={3,-3,4};extreme.alpha={20,-20,20};
  extreme.shake={3,-3,3};
 for(int i=0;i<120;i++)l.step(1.f/60,extreme);
 for(int i=0;i<40;i++){
  float p[3]={-.8f+.04f*i,.4f,.45f},n[3]={0,0,1};float base[3];std::copy(p,p+3,base);l.deform(base,n);
  V jac[3];for(int k=0;k<3;k++){float pp[3]={p[0],p[1],p[2]},nn[3]={0,0,1};pp[k]+=.001f;l.deform(pp,nn);jac[k]={(pp[0]-base[0])*1000,(pp[1]-base[1])*1000,(pp[2]-base[2])*1000};}
  assert(std::abs(dot(cross(jac[0],jac[1]),jac[2])-1)<.004f);
  assert(std::abs(dot({n[0],n[1],n[2]},jac[0]))<.003f && std::abs(dot({n[0],n[1],n[2]},jac[1]))<.003f);
 }
 Liquid a30,a60;for(int i=0;i<60;i++)a30.step(1.f/30,extreme);for(int i=0;i<120;i++)a60.step(1.f/60,extreme);assert(std::abs(a30.x.p-a60.x.p)<.005f);
 // Lower viscosity deliberately retains a longer tail, but still settles.
 for(int i=0;i<2000;i++)l.step(1.f/120,{});assert(l.activity()<.0001f);
 Blob::Physics combo;
 for(int i=0;i<30000;i++) {
  Sample s=stationary();s.linear={3*std::sin(i*.7f),3*std::cos(i*.6f),3*std::sin(i*.9f)};s.omega=s.linear*3;s.alpha=s.linear*10;
  combo.step((i%4+1)/120.f,{i%9!=0,i*17%410,i*23%502,205,290,uint32_t(i/9+1)},false,s);
  float p[]={.2f,.4f,.45f},n[]={0,0,1},out[3],normal[3];combo.deform(p,n,out,normal);
  for(int k=0;k<3;k++)assert(std::isfinite(out[k])&&std::isfinite(normal[k]));
  assert(combo.liquid.activity()<3.6f && std::abs(combo.liquid.x.p)<=.2001f);
 }
 combo.step(.033f,{},true,extreme);assert(combo.activity()==0 && combo.liquid.excitement==0);
 puts("Six-axis mapping, gyro gravity fusion, translation versus tilt, bias/noise, all six reactions, momentum, settling, 30/60 Hz consistency, volume determinant, normals, combined stress and sleep reset passed.");
}
