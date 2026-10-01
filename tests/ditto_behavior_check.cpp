#include "ditto_behavior.h"
#include "ditto_eyes.h"
#include <cassert>
#include <cstdio>
using namespace DittoBehavior;
struct Run {
 Controller c;Blob::Physics p;Blob::Touch t;BlobMotion::Sample sensor;
 void frame(float dt=1.f/120,bool sleeping=false,bool speaking=false){p.step(dt,t,sleeping,sensor);c.step(dt,t,p.impact_hit,sensor,p.liquid,sleeping,speaking,.5f);}
 void time(float seconds){int n=int(std::round(seconds*120));for(int i=0;i<n;i++)frame();}
 void press(int x=205,int y=306){t={true,x,y,x,y,t.press+1};p.setImpact(t.press,true,0,-.2f,0,0,.5f,1);frame();}
 void release(){t.down=false;frame();}
};
int main(){
 // Accepted single-tap timeline is relative to contact, independent of uptime.
 for(float pre:{0.f,7.3f,123.f}){
  Run r;r.time(pre);r.press();float at=r.c.last_contact;
  auto reach=[&](float age){while(r.c.now-at<age-.004f)r.frame();};
  reach(.15);r.release();reach(.22);assert(r.c.pose.mouth.oval>.999f);
  // Every oval now holds 2.3 s. Check inside the added time,
  // immediately before opening, and after the unchanged opening/recovery.
  reach(1.40);assert(r.c.pose.mouth.oval>.999f && r.c.pose.mouth.melon==0);
  reach(2.48);assert(r.c.pose.mouth.oval>.999f && r.c.pose.mouth.melon==0);
  reach(2.75);assert(r.c.pose.mouth.melon>.999f);
  reach(3.31);assert(r.c.pose.mouth.melon>.999f);
  reach(3.86);assert(r.c.pose.mouth.oval==0 && r.c.pose.mouth.melon==0);
 }
 // Every poke reaches physics; a burst extends one face sequence.
 Run repeat;repeat.press();repeat.time(.15);repeat.release();repeat.time(.44);
 repeat.press();float opening=repeat.c.melon_at;repeat.time(.15);repeat.release();repeat.time(.44);
 repeat.press();assert(repeat.c.group==3 && repeat.c.melon_at>opening+.5f);
 repeat.time(.15);repeat.release();repeat.time(.65);assert(std::abs(repeat.c.pose.gaze_y)<.025f);
 repeat.time(6.5);assert(repeat.p.tap_count==3 && repeat.c.mode==Mode::Idle);
 // A tap becomes a drag without a mouth discontinuity or obsolete tap replay.
 Run drag;drag.press();drag.time(.15);drag.release();drag.time(.45);drag.press();
 for(int i=0;i<28;i++){drag.t.x-=2;drag.t.y-=1;auto before=drag.c.pose.mouth;drag.frame();assert(std::abs(before.oval-drag.c.pose.mouth.oval)<.16f);}
 assert(drag.p.poke.v!=0 && (drag.c.mode==Mode::Pull||drag.c.mode==Mode::Flick));
 drag.time(1);assert(drag.c.mode==Mode::Hold);drag.time(1);assert(std::abs(drag.c.pose.gaze_x)<.015f);
 drag.release();drag.time(2.1);assert(drag.c.pose.mouth.oval>.999f && drag.c.pose.mouth.melon==0);
 drag.time(2.6);assert(drag.c.mode==Mode::Idle && drag.c.pose.mouth.melon==0);
 // Every non-tap route keeps its oval: finite flick, slow pull release,
 // held contact release, swing stop and shake stop.
 for(int kind=0;kind<5;kind++) {
  Controller c;Blob::Touch t;BlobMotion::Sample m;BlobMotion::Liquid l;
  auto frame=[&](){c.step(1.f/120,t,true,m,l,false,false,0);};
  auto wait=[&](float seconds){for(int i=0;i<int(std::round(seconds*120));i++)frame();};
  if(kind<3) {
   t={true,205,300,205,300,1};frame();
   if(kind==0){t.x+=35;frame();assert(c.mode==Mode::Flick);}
   else if(kind==1){wait(.25);for(int i=0;i<20;i++){t.x++;frame();}assert(c.mode==Mode::Pull);}
   else {wait(.6);assert(c.mode==Mode::Hold);}
   t.down=false;frame();
  } else {
   m.valid=true;
   if(kind==3)m.linear.x=.2f;else l.recovery.shaking=true;
   wait(.7);assert(c.mode==(kind==3?Mode::Swing:Mode::Shake));
   m.linear={};l.recovery.shaking=false;wait(.25);
  }
  wait(.5);assert(c.pose.mouth.oval>.999f && c.pose.mouth.melon==0);
  wait(1.5);assert(c.pose.mouth.oval>.999f && c.pose.mouth.melon==0);
  // A fresh touch immediately owns the expression and can become a drag.
  t={true,205,300,205,300,t.press+1};frame();assert(c.mode==Mode::Tap);
  t.x+=35;frame();assert(c.mode==Mode::Flick);
  t.down=false;frame();wait(7);assert(c.mode==Mode::Idle);
 }
 // Voice and sleep supersede obsolete facial work, but not contact physics.
 Run mixed;mixed.press();mixed.time(.1);mixed.release();for(int i=0;i<140;i++)mixed.frame(1.f/120,false,true);
 mixed.frame();mixed.time(.7);assert(mixed.c.mode==Mode::Idle && mixed.c.pose.mouth.melon<.01f);
 for(int i=0;i<150;i++)mixed.frame(1.f/120,true);assert(mixed.c.pose.sleep==1);
 for(int i=0;i<30;i++)mixed.frame();assert(mixed.c.pose.sleep==0 && mixed.c.mode==Mode::Idle);
 // Real sensor classifier: no recovery for noise, tilt, pickup or slow swing.
 for(int type=0;type<4;type++){
  BlobMotion::Liquid l;
  for(int i=0;i<1200;i++){
   float t=i/120.f;BlobMotion::Sample s;s.valid=true;s.gravity={0,-1,0};
   if(type==0)s.shake={.008f*std::sin(80*t),0,0};
   if(type==1){s.gravity={std::sin(.4f*std::sin(t)),-std::cos(.4f*std::sin(t)),0};s.omega.z=.4f*std::cos(t);}
   if(type==2 && t<.2f){s.linear.y=.8f;s.shake.y=.3f;}
   if(type==3){s.linear.x=.21f*std::sin(6.2831853f*.85f*t);s.shake.x=.065f*std::sin(6.2831853f*.85f*t);}
   l.step(1.f/120,s);assert(l.damping_scale==1 && !l.recovery.shaking);
  }
 }
 // Qualify rapid alternating force on every axis; stop ramps only damping.
 for(int axis=0;axis<3;axis++){
  BlobMotion::Liquid l;BlobMotion::Sample s;s.valid=true;s.gravity={0,-1,0};bool seen=false;
  for(int i=0;i<360;i++){
   float a=.30f*std::sin(6.2831853f*3*i/120.f);s.shake={};(&s.shake.x)[axis]=a;l.step(1.f/120,s);
   seen|=l.recovery.shaking;assert(l.damping_scale==1);
  }
  assert(seen);s.shake={};for(int i=0;i<35;i++)l.step(1.f/120,s);
  assert(l.recovery.recovering && std::abs(l.damping_scale-3.5f)<.001f);
  // A gentle tilt or swing after recovery restores the baseline coefficients,
  // not only another violent shake.
  s.omega.z=.15f;l.step(1.f/120,s);assert(l.damping_scale==1);
  s.omega={};s.shake.x=.2f;l.step(1.f/120,s);assert(l.damping_scale==1);
 }
 // Arbitrary interrupted combinations run at uneven frame intervals; there
 // is one bounded finite face, and physical contacts still enter immediately.
 Run random;unsigned rng=145;
 auto next=[&](){rng=rng*1664525u+1013904223u;return rng;};
 for(int i=0;i<12000;i++) {
  if(next()%89==0){random.press(90+int(next()%220),170+int(next()%180));}
  if(next()%73==0)random.t.down=false;
  random.t.x=std::clamp(random.t.x+int(next()%17)-8,30,380);
  random.t.y=std::clamp(random.t.y+int(next()%17)-8,100,420);
  random.sensor.valid=true;random.sensor.gravity={.2f,-.9f,0};
  random.sensor.shake={.2f*std::sin(i*.53f),.1f*std::cos(i*.53f),0};
  random.frame(.012f+(next()%40)*.001f,i%500>450,i%241<90);
  auto p=random.c.pose;
  assert(std::isfinite(p.gaze_x)&&std::isfinite(p.gaze_y));
  assert(p.mouth.oval>=0 && p.mouth.melon>=0 && p.mouth.oval+p.mouth.melon<1.001f);
  assert(p.sleep>=0 && p.sleep<=1);
 }
 // Only sustained recent shake reversals earn the spiral/wavy-mouth state.
 for(int strong=0;strong<2;strong++) {
  Run dizzy;dizzy.sensor.valid=true;dizzy.sensor.gravity={0,-1,0};
  for(int i=0;i<420;i++) {
   dizzy.sensor.shake.x=(strong?.30f:.025f)*std::sin(6.2831853f*3*i/120.f);
   dizzy.frame();if(!strong)assert(dizzy.c.pose.dizzy==0);
  }
  if(strong) {
   assert(dizzy.c.pose.dizzy>.99f);
   dizzy.sensor.shake={};dizzy.time(1);assert(dizzy.c.pose.dizzy>.99f);
   dizzy.time(5);assert(dizzy.c.pose.dizzy==0);
   for(int i=0;i<420;i++){dizzy.sensor.shake.x=.30f*std::sin(6.2831853f*3*i/120.f);dizzy.frame();}
   assert(dizzy.c.pose.dizzy>.99f);dizzy.press();assert(dizzy.c.pose.dizzy==0);
  }
 }
 DittoEyes::Controller eye;
 auto spiral=eye.resolvePose(2.65,0,1,1,true,0,0,1);
 assert(spiral.spiral==1 && spiral.open[0]==1 && spiral.open[1]==1);
 spiral=eye.resolvePose(2.65,1,1,1,true,0,0,1);
 assert(spiral.spiral==0 && std::abs(spiral.open[0]-.24f)<1e-6f);
 auto e=eye.resolvePose(2.65,1,1,1,true,.03f,-.018f);
 assert(std::abs(e.open[0]-.24f)<1e-6f && e.open[1]==e.open[0] && std::abs(e.width-1.65f)<1e-6f && e.cross==0);
 e=eye.resolvePose(2.65,0,1,1,true,0,0);assert(e.open[0]>.139f && e.open[1]==1 && e.cross==0);
 e=eye.resolvePose(2.65,0,0,1,false,0,0);assert(e.open[0]>.049f && e.open[0]<.051f);
 puts("Tap timing, repeats, interrupts, hold/release, speech, sleep, all-axis shake classification, slow-motion exclusions and eye arbitration passed.");
}
