#include "blob_physics.h"
#include <cassert>
#include <cstdio>
int main(){
  Blob::Physics p;p.setImpact(1,true,0,0,0,0,.5f,1);
  Blob::Touch touch{true,205,290,205,290,1};
  float dent=0,ring=0,depth=0;int dent_frame=0,ring_frame=0;
  for(int i=0;i<120;i++){
    touch.down=i<5;p.step(1.f/120,touch);
    if(p.poke.p>dent){dent=p.poke.p;dent_frame=i;}
    if(p.ripple.p<ring){ring=p.ripple.p;ring_frame=i;}
    depth=std::min(depth,p.bounce_z.p);
    assert(p.squash.p==0 && p.dent.p==0);
  }
  assert(dent>.20f && ring<-.05f && ring_frame>dent_frame && depth<-.05f);
  printf("Center hit: dent %.4f at %.3fs; ring %.4f at %.3fs; whole-body depth %.4f\n",dent,dent_frame/120.f,ring,ring_frame/120.f,depth);
  // Lift changes classification only: no release kick or repeated held impulse.
  Blob::Physics down;down.setImpact(1,true,-.5f,.2f,-.8f,0,.5f,.6f);
  down.step(1.f/60,{true,130,261,130,261,1});auto released=down;
  for(int i=0;i<120;i++){
    down.step(1.f/60,{true,130,261,130,261,1});released.step(1.f/60,{false,130,261,130,261,1});
    assert(down.bounce_x.p==released.bounce_x.p && down.bounce_y.p==released.bounce_y.p && down.bounce_z.p==released.bounce_z.p && down.ripple.p==released.ripple.p);
  }
  assert(released.tap_count==1 && down.tap_count==0);
  // A rounded footprint: half amplitude at about 47px, not the old 29px.
  Blob::Physics shape;shape.poke.p=.25f;const float normal[]={0,0,1};float o[3],n[3];
  float half[]={.322f,0,.5f};shape.deform(half,normal,o,n);
  assert(std::abs((half[2]-o[2])/.25f-.5f)<.002f);
  // Both scalar kernels share one direction; bound the exact Jacobian under
  // every allowed dent/ripple sign combination, including simultaneous peaks.
  float min_det=1;
  for(float d:{-.31f,.31f})for(float r:{-.17f,.17f})for(int i=0;i<20000;i++){
    float radius=i*.0001f,core=1/(1+4*radius*radius),outer=1/(1+radius*radius);
    float gc=-16*radius*core*core*core,go=-4*radius*outer*outer*outer;
    min_det=std::min(min_det,1-std::abs(d*gc+r*(go-gc)));
  }
  assert(min_det>.12f);printf("Combined dent/ring Jacobian stays above %.4f\n",min_det);
  for(int i=0;i<1800;i++)p.step(1.f/120,{});assert(p.activity()<.001f);
  p.step(.033f,{true,205,290,205,290,2});p.step(.033f,{},true);
  assert(p.activity()==0 && p.bounce_z.v==0 && p.ripple.v==0);
  puts("Rounded footprint, delayed ring, center depth nudge, one contact impulse, no release kick, bounded normals, settling and sleep passed.");
}
