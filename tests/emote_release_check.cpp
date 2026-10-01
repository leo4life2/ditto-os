#include "blob_render.h"
#include "ditto_autoplay.h"
#include <cassert>
#include <cstdio>
static float strength(const DittoRig::Skeleton& rig){
 float p=0;for(auto& b:rig.pose.bone)p=std::max({p,std::abs(b.rx),std::abs(b.ry),std::abs(b.rz),std::abs(b.x),std::abs(b.y),std::abs(b.z)});return p;
}
int main(){
 constexpr float dt=1.f/30;float old_peak=0;
 // Real contact/release reproducer, not a disconnected avatar.emote call.
 BlobRenderer old;Blob::Touch held{true,242,260,242,260,1};
 old.physics.setImpact(1,true,.2f,.1f,0,0,.5f,1);
 for(int i=0;i<75;i++){old.setEmote(Avatar::Listening,i*dt);old.advance(dt,held,false);}
 assert(old.behavior.mode==DittoBehavior::Mode::Hold);
 held.down=false;
 for(int i=0;i<90;i++){old.setEmote(Avatar::Celebrate,i*dt);old.advance(dt,held,false);if(i>15)old_peak=std::max(old_peak,strength(old.rig));}
 printf("Legacy release: max skeletal movement during first 3 seconds = %.8f\n",old_peak);
 assert(old_peak<.001f);
#ifdef RECOVERY_PRIORITY
 for(int mode=0;mode<3;mode++)for(int clip:DittoAutoplay::reactions){
  BlobRenderer b,reference;Blob::Touch touch;
  auto step=[&](int current,float seconds,bool priority,bool sleep=false,bool speaking=false){
   b.setEmote(current,seconds,priority);reference.setEmote(current,seconds);
   b.advance(dt,touch,sleep,{},nullptr,speaking);reference.advance(dt,touch,sleep,{},nullptr,speaking);
   // Compare actual liquid deformation across the full experiment. Authored
   // pose ownership cannot alter the integrator, momentum or surface response.
   for(int x=-2;x<=2;x++)for(int y=-2;y<=2;y++){
    float p[]={x*.28f,y*.25f,.4f},n[]={0,0,1},a[3],an[3],c[3],cn[3];
    b.physics.deform(p,n,a,an);reference.physics.deform(p,n,c,cn);
    for(int k=0;k<3;k++){assert(a[k]==c[k]);assert(an[k]==cn[k]);}
   }
  };
  if(mode<2){
   touch={true,242,260,242,260,1};b.physics.setImpact(1,true,.2f,.1f,0,0,.5f,1);reference.physics.setImpact(1,true,.2f,.1f,0,0,.5f,1);
   if(mode==0){for(int i=0;i<75;i++)step(Avatar::Listening,i*dt,false);}
   else {step(Avatar::Idle,0,false);touch.down=false;step(Avatar::Idle,dt,false);touch.down=true;touch.press=2;b.physics.setImpact(2,true,.2f,.1f,0,0,.5f,1);reference.physics.setImpact(2,true,.2f,.1f,0,0,.5f,1);step(Avatar::Idle,2*dt,false);}
   touch.down=false;
  } else {
   for(auto* renderer:{&b,&reference}){renderer->behavior.mode=DittoBehavior::Mode::Shake;renderer->behavior.released=0;renderer->behavior.release();renderer->behavior.dizzy_remaining=4;renderer->behavior.pose.dizzy=1;}
  }
  float peak=0,wink=0;
  for(int i=0;i<90;i++){
   step(clip,i*dt,true);peak=std::max(peak,strength(b.rig));wink=std::max(wink,b.rig.pose.wink);
   assert(b.reaction_face && b.presentation_face.quiet && b.presentation_face.dizzy==0);
   assert(b.presentation_face.mouth.oval==0);
  }
  assert(peak>.02f);if(clip==Avatar::Wink)assert(wink>.99f);
  // Immediate fresh contact and sleep/speech regain priority.
  touch.down=true;step(clip,.7f,true);assert(!b.reaction_face);
  touch.down=false;step(clip,.7f,true,true);assert(!b.reaction_face);
  step(clip,.7f,true,false,true);assert(!b.reaction_face);
  // Motion can keep the scheduler busy after a clip finishes. A stale ownership
  // flag must never keep suppressing physical expressions in the idle clip.
  step(Avatar::Idle,0,true);assert(!b.reaction_face);
 }
 puts("PASS: 21 hold/double-tap/shake release cases; visible head/hand movement, wink, physics identity, new-touch/sleep/speech priority");
#endif
}
