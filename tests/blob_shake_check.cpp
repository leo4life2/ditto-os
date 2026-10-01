#include "blob_physics.h"
#include <cassert>
#include <cstdio>
using namespace BlobMotion;
static float wave(const Liquid &l,int axis){return axis==0?l.wave_x.p:axis==1?l.wave_y.p:l.wave_z.p;}
static void axisValue(V &v,int axis,float value){if(axis==0)v.x=value;else if(axis==1)v.y=value;else v.z=value;}
int main(){
  constexpr float pi=3.14159265359f;
  // IMU at 60 Hz, display/physics at 30 Hz, matching the device's consumers.
  for(int axis=0;axis<3;axis++)for(float hz:{2.f,3.f,4.f,5.f,6.f}) {
    Filter f;Liquid l;int64_t now=1000000;f.feed({0,0,1},{},now);
    float low=0,high=0,tail=0;
    for(int i=1;i<=360;i++) {
      V a{0,0,1},delta;axisValue(delta,axis,2.f*std::sin(2*pi*hz*i/60.f));
      now+=16667;auto sample=f.feed(a+delta,{},now);
      if(i%2==0)l.step(1.f/30,sample);
      if(i>120){low=std::min(low,wave(l,axis));high=std::max(high,wave(l,axis));}
      assert(finite(sample.shake));
      assert(sample.gravity.z<-.99f); // Shaking must not become false tilt.
    }
    for(int i=1;i<=480;i++) {
      now+=16667;auto sample=f.feed({0,0,1},{},now);
      if(i%2==0)l.step(1.f/30,sample);
      if(i<=30)tail=std::max(tail,std::abs(wave(l,axis)));
    }
    printf("axis=%d hz=%.0f range=%.4f tail=%.4f settled=%.6f\n",axis,hz,high-low,tail,std::abs(wave(l,axis)));
    assert(low<-.025f && high>.025f && high-low>.09f);
    assert(tail>.012f && std::abs(wave(l,axis))<.001f);
  }
  Filter rest;Liquid l;int64_t now=1000000;
  for(int i=0;i<600;i++){
    now+=16667;auto s=rest.feed({.008f*std::sin(i*2.f),0,1},{},now);l.step(1.f/60,s);
    assert(s.shake.length()<.001f && std::abs(l.wave_x.p)<.001f);
  }
  // A sudden acceleration pulse enters with the selected positive sign.
  Filter pulse;pulse.feed({0,0,1},{},10000);Liquid p;
  p.step(1.f/30,pulse.feed({2,0,1},{},43333));assert(p.wave_x.p>0);
  // Constant acceleration eventually leaves the new band-pass modes alone.
  for(int i=0;i<600;i++)p.step(1.f/60,pulse.feed({2,0,1},{},60000+i*16667));
  assert(std::abs(p.wave_x.p)<.001f);
  Blob::Physics b;Sample s;s.valid=true;s.shake={4,-4,4};
  for(int i=0;i<10000;i++){
    s.shake={4*std::sin(i*.7f),4*std::sin(i*.9f),4*std::cos(i*.8f)};
    b.step((i%4+1)/120.f,{i%9!=0,i*17%410,i*23%502,205,290,uint32_t(i/9+1)},false,s);
    float pos[]={.3f,.5f,.4f},n[]={0,0,1},out[3],normal[3];b.deform(pos,n,out,normal);
    for(int k=0;k<3;k++)assert(std::isfinite(out[k])&&std::isfinite(normal[k]));
    assert(std::abs(b.liquid.wave_x.p)<=.44001f && std::abs(b.liquid.wave_y.p)<=.36001f && std::abs(b.liquid.wave_z.p)<=.40001f);
  }
  b.step(.033f,{},true,s);assert(b.liquid.activity()==0 && b.liquid.wave_x.v==0);
  puts("2–6 Hz all-axis reversals, after-wobble, settling, noise rejection, selected direction, bounded combined touch/shake and sleep passed.");
}
