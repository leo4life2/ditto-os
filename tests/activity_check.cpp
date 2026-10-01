#include "../main/activity.h"
#include <cassert>
#include <cstdio>
#include <limits>
int main() {
  Activity::Motion detector;
  BlobMotion::Sample s;
  s.valid=true;s.gravity={0,0,1};s.specific={0,0,1};s.time_us=1000000;
  auto step=[&](int dt=16000) {s.time_us+=dt;return detector.observe(s);};
  // Long stationary run, representative sensor noise, no artificial activity.
  for(int i=0;i<4500;i++) {
    s.specific={.009f*sinf(i),.007f*cosf(i),1+.011f*sinf(i*.3f)};
    s.omega={.014f,.003f,-.004f};assert(!step());
  }
  // One spurious sample isn't a pickup; a sustained gentle movement is.
  s.specific={.1f,0,1};assert(!step());s.specific={0,0,1};assert(!step());
  for(int axis=0;axis<3;axis++) {
    detector={};s.gravity={0,0,1};s.specific={0,0,1};s.omega={};for(int i=0;i<12;i++)step();
    (&s.specific.x)[axis]+=.10f;
    assert(!step());assert(!step());for(int i=0;i<5;i++)step();assert(step());
    s.specific={0,0,1};for(int i=0;i<25;i++)step();assert(!step());
    (&s.omega.x)[axis]=.2f;assert(!step());assert(!step());assert(step());
  }
  // A slow tilt below the angular-velocity threshold accumulates orientation.
  detector={};s.omega={};s.specific={0,0,1};step();bool saw=false;
  for(int i=0;i<170;i++) {float a=i*.00032f;s.specific={sinf(a),0,cosf(a)};saw|=step();}
  assert(saw);for(int i=0;i<80;i++)step();assert(!step());
  // A sensor reset / stale gap establishes a reference, not a fake movement.
  s.specific={1,0,0};assert(!step(200000));assert(!step());
  s.valid=false;assert(!step());s.valid=true;
  s.specific.x=std::numeric_limits<float>::quiet_NaN();assert(!step());
  // The real unit's ~5 deg/s gyro offset changes the fused gravity estimate
  // during startup calibration, but must never count as measured movement.
  BlobMotion::Filter filter;Activity::Motion startup;
  for(int i=0;i<700;i++) {
    auto sample=filter.feed({-.004f,.004f,1.004f},{-.013f,.091f,.005f},1000000+i*16000);
    assert(!startup.observe(sample));
  }
  Activity::Motion noisy;
  for(int i=0;i<5000;i++) {
    BlobMotion::Sample sample;sample.valid=true;sample.time_us=1000000+i*16000;
    sample.specific={-.010f+.014f*std::cos(i*1.8f),.004f+.010f*std::sin(i*1.4f),1.004f+.010f*std::sin(i*2.1f)};
    assert(!noisy.observe(sample));
  }
  auto sleep=[](int64_t elapsed,bool touch=false,bool moving=false,bool voice=false,bool fresh=true) {
    return Activity::shouldSleep(1000000+elapsed,1000000,touch,moving,voice,fresh);
  };
  assert(!sleep(59999999));assert(sleep(60000000));
  assert(!sleep(70000000,true));assert(!sleep(70000000,false,true));
  assert(!sleep(70000000,false,false,true));assert(!sleep(70000000,false,false,false,false));
  puts("Rest noise, sustained pickup/rotation, slow tilt, stale sensor and inactivity deadline passed.");
}
