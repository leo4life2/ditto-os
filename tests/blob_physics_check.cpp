#include "blob_physics.h"
#include "touch_input.h"
#include <cassert>
#include <cstdio>

static Blob::Physics drag(float hz) {
  Blob::Physics p; Blob::Touch t{true,205,290,205,290,1};
  for(int i=0;i<int(hz);i++) {
    t.x=205+int(90*i/hz); p.step(1/hz,t);
  }
  t.down=false;
  for(int i=0;i<int(hz*.4f);i++) p.step(1/hz,t);
  return p;
}
int main() {
  TouchRecovery recovery;
  for(int i=0;i<7;i++)assert(!recovery.observe(false,i*16000));
  assert(recovery.observe(false,112000));
  for(int i=0;i<100;i++)assert(!recovery.observe(false,130000+i*16000));
  assert(recovery.observe(false,2200000));
  assert(!recovery.observe(true,4500000));
  for(int i=0;i<7;i++)assert(!recovery.observe(false,4500000+i*16000));
  assert(recovery.observe(false,4612000));
  for(int i=0;i<100;i++)assert(!recovery.observe(false,5000000+i*500000,true));
  for(int i=0;i<7;i++)assert(!recovery.observe(false,60000000+i*10000));
  assert(recovery.observe(false,60070000)); // Real report-ready failure still recovers.
  TouchPoint p;
  uint8_t down[]={1,0,123,0,234}; assert(decode_touch(down,p)&&p.down&&p.x==123&&p.y==234);
  uint8_t move[]={1,0x81,0x20,0x01,0x70};assert(decode_touch(move,p)&&p.x==288&&p.y==368);
  uint8_t up[]={0,0xff,0xff,0xff,0xff};assert(decode_touch(up,p)&&!p.down);
  uint8_t invalid[]={1,0,0,0x0f,0xff};assert(!decode_touch(invalid,p));
  Blob::Physics left,right;
  left.step(.033f,{false,110,290,110,290,1}); right.step(.033f,{false,300,290,300,290,1});
  assert(left.squash.p==0 && right.squash.p==0 && left.dent.p==0 && right.dent.p==0); // Taps never flatten the whole body.
  assert(left.tap_count==1 && right.tap_count==1 && left.bounce_x.p>0 && right.bounce_x.p<0);
  Blob::Physics fast_swipe;fast_swipe.setImpact(1,true,0,0,0,0);
  fast_swipe.step(.033f,{false,300,290,205,290,1});assert(fast_swipe.tap_count==0);
  Blob::Physics focused;focused.setImpact(1,true,0,0,0,0,.5f,1);
  Blob::Touch tap{true,205,290,205,290,1};
  for(int i=0;i<4;i++)focused.step(1.f/60,tap);
  assert(focused.poke.p>.09f && focused.squash.p==0 && focused.dent.p==0);
  tap.down=false;focused.step(1.f/60,tap);
  assert(focused.tap_count==1 && focused.squash.p==0 && focused.dent.p==0);
  focused.liquid={};focused.material={}; // Isolate local footprint from the new body-wave response.
  const float skin_n[]={0,0,1},center[]={0,0,.5f},far[]={.9f,0,.5f};float pc[3],pf[3],nn[3];
  focused.deform(center,skin_n,pc,nn);focused.deform(far,skin_n,pf,nn);
  assert(std::abs(pf[2]-far[2])<.12f*std::abs(pc[2]-center[2]));
  // Lighting normals stay tangent to the rounded dent and delayed ring.
  for(float amplitude : {-.31f,.31f}) {
    focused.poke.p=amplitude;
    for(int i=-8;i<=8;i++) {
      float pos[]={i*.04f,.07f,.5f},p0[3],px[3],py[3],normal[3],discard[3];
      focused.deform(pos,skin_n,p0,normal);pos[0]+=.0001f;focused.deform(pos,skin_n,px,discard);
      pos[0]-=.0001f;pos[1]+=.0001f;focused.deform(pos,skin_n,py,discard);
      float a=0,b=0;for(int k=0;k<3;k++){a+=(px[k]-p0[k])*normal[k]/.0001f;b+=(py[k]-p0[k])*normal[k]/.0001f;}
      assert(std::abs(a)<.003f && std::abs(b)<.003f);
    }
  }
  Blob::Physics missed;missed.setImpact(1,false);missed.step(.033f,{false,205,290,205,290,1});
  assert(missed.tap_count==0 && missed.activity()==0);
  Blob::Physics top,bottom;top.setImpact(1,true,0,.7f,0,.8f);bottom.setImpact(1,true,0,-.6f,0,-.6f);
  top.step(.033f,{false,205,200,205,200,1});bottom.step(.033f,{false,205,380,205,380,1});
  assert(top.bounce_y.p<0 && bottom.bounce_y.p>0);
  Blob::Physics rebound;Blob::Touch held{true,205,290,205,290,1};
  for(int i=0;i<40;i++){held.x=295;rebound.step(1.f/30,held);}
  held.down=false;float overshoot=0;
  for(int i=0;i<30;i++){rebound.step(1.f/30,held);overshoot=std::min(overshoot,rebound.pull_x.p);}
  assert(overshoot<-.15f && rebound.tap_count==0); // Lively release without tap impulse on a drag.
  auto a=drag(30),b=drag(60);assert(std::abs(a.x.p-b.x.p)<.015f);
  assert(a.activity()>.005f); // Release retains momentum.
  for(int i=0;i<1800;i++)a.step(1.f/120,{});
  assert(a.activity()<.001f && a.annoyance<.01f);
  float directions[2]={};
  for(int sign=0;sign<2;sign++) {
    Blob::Physics v; Blob::Touch t{true,270,290,270,290,1};
    for(int i=0;i<90;i++) {
      float angle=(sign?1:-1)*i*.07f;
      t.x=205+int(65*std::cos(angle));t.y=290-int(65*std::sin(angle));v.step(1.f/60,t);
    }
    directions[sign]=v.roll.p;
    assert(v.annoyance>.3f);
  }
  assert(directions[0]*directions[1]<0); // Circles carry directional torque.
  Blob::Physics v;
  for(int i=0;i<50000;i++) {
    Blob::Touch t{(i%13)!=0,int(i*31%410),int(i*47%502),205,290,uint32_t(i/13+1)};
    v.step((i%7)*.033f,t);
    assert(std::isfinite(v.activity()) && std::abs(v.x.p)<=.081f && std::abs(v.squash.p)<=.251f && v.stretch()<.781f);
  }
  v.dent.p=.3f;v.dent_x=.1f;v.dent_y=.2f;v.pull_x.p=.25f;v.pull_y.p=-.3f;v.slosh_x.p=.12f;v.slosh_y.p=-.08f;
  const float pos[]={.1f,.3f,.5f},normal[]={0,0,1};float out[3],n[3],dx[3],dy[3],unused[3];
  v.deform(pos,normal,out,n);
  float px[]={pos[0]+.0001f,pos[1],pos[2]},py[]={pos[0],pos[1]+.0001f,pos[2]};
  v.deform(px,normal,dx,unused);v.deform(py,normal,dy,unused);
  float dotx=0,doty=0;for(int k=0;k<3;k++){dotx+=(dx[k]-out[k])*n[k]/.0001f;doty+=(dy[k]-out[k])*n[k]/.0001f;}
  assert(std::abs(dotx)<.002f && std::abs(doty)<.002f);
  Blob::Physics local;local.dent_x=0;local.dent_y=0;local.pull_x.p=.5f;
  float nearP[]={0,0,.5f},farP[]={1.2f,-.8f,.1f},nearOut[3],farOut[3];
  local.deform(nearP,normal,nearOut,unused);local.deform(farP,normal,farOut,unused);
  assert(nearOut[0]-nearP[0] > 5*(farOut[0]-farP[0])); // Local skin stretch, not a rigid move.
  Blob::Physics hold;Blob::Touch finger{true,205,290,205,290,1};
  for(int i=0;i<40;i++)hold.step(1.f/30,finger);
  assert(hold.dent.p>.24f && hold.x_eyes>.9f); // Holding visibly indents and squishes the face.
  finger.down=false;for(int i=0;i<55;i++)hold.step(1.f/30,finger);
  assert(hold.x_eyes<.01f && hold.side_eye<.01f); // A stationary hold recovers to normal open eyes.
  v.step(.033f,{},true);assert(v.activity()==0 && v.x_eyes==0 && v.side_eye==0);
  puts("Touch decode, directional impulses, momentum, circles, settling, frame-rate consistency, bounded stress, deformation normals local skin stretch, expressive recovery and sleep reset passed");
}
