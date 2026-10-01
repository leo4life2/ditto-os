#include "ditto_eyes.h"
#include "ditto_rig.h"
#include <cassert>
#include <cstdio>
int main(){
 DittoEyes::Controller c;
 auto p=c.resolve(1,false,0,0,0,0);assert(p.open[0]==1 && p.open[1]==1);
 p=c.resolve(2.65f,false,0,0,1,-1);assert(p.open[0]<.061f && p.open[1]==p.open[0] && p.gaze<-.04f);
 p=c.resolve(2.65f,false,.5f,0,1,1);assert(std::abs(p.open[0]-.65f)<1e-6f && p.open[1]==1 && p.chevron==0);
 p=c.resolve(2.67f,false,0,0,0,0);assert(p.open[0]==1 && p.open[1]==1); // no partial blink on release
 p=c.resolve(3,false,0,0,0,0);p=c.resolve(6.65f,false,0,0,0,0);assert(p.open[0]<.061f);
 p=c.resolve(2.65f,false,1,.7f,1,1);assert(p.open[0]==1 && p.open[1]==1 && p.chevron==1 && p.cross==0);
 p=c.resolve(2.65f,true,1,1,1,1);assert(std::abs(p.open[0]-.24f)<1e-6f && p.open[1]==p.open[0] && p.width==1.65f && p.cross==0);
 for(int i=0;i<600;i++){
  float time=i/120.f,wink=(i%61)/60.f,cross=i%3==0?(i%47)/46.f:0;
  p=c.resolve(time,false,wink,cross,.7f,.9f);
  assert(p.cross==0);
  assert(p.open[0]>=.059f && p.open[0]<=1 && p.open[1]>=.059f && p.open[1]<=1);
 }
 // Closed eyelid has a modest fixed footprint, with a valid fan.
 for(int i=0;i<21;i++) {
  assert(std::abs(DittoEyes::Chevron[i][0])<=.043f);
  assert(std::abs(DittoEyes::Chevron[i][1])<=.029f);
 }
 for(int i=1;i<=20;i++) {
  int j=i%20+1;const auto& c=DittoEyes::Chevron[0];const auto& a=DittoEyes::Chevron[i];const auto& b=DittoEyes::Chevron[j];
  assert((a[0]-c[0])*(b[1]-c[1])-(a[1]-c[1])*(b[0]-c[0])>0);
 }
 // Exercise the actual rig-to-eyelid path at the display cadence. Only one
 // eye closes, the > is a fixed replacement, and no body easing delays it.
 DittoRig::Skeleton rig;DittoEyes::Controller winkEyes;
 int affected=0,closed=0;
 for(int frame=0;frame<60;frame++) {
  float t=frame/30.f;rig.step(1.f/30,Avatar::Wink,t,true);
  p=winkEyes.resolvePose(t,0,rig.pose.wink,0,true,0,0);
  assert(p.open[1]==1);assert(p.chevron==0||p.chevron==1);
  if(p.open[0]!=1||p.chevron!=0) {
   affected++;assert(t>.750f&&t<1.265f);
  }
  if(p.chevron!=0)closed++;
 }
 assert(affected>=15&&affected<=16);assert(closed==13);
 rig.step(1.f/30,Avatar::Wink,.82f,true);assert(rig.pose.wink==1);
 rig.step(1.f/30,Avatar::Wink,.83f,false);assert(rig.pose.wink==0);
 puts("Dot gaze, normal blink, exclusive sleep/wink, fixed eyelid bounds, 515 ms wink and immediate interruption passed.");
}
