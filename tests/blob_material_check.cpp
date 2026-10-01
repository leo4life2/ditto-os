#include "blob_physics.h"
#include <cassert>
#include <cstdio>
static void tick(Blob::Material &m,float seconds,float x,float y,float z,int hz=120) {
  for(int i=0;i<int(seconds*hz);i++)m.step(1.f/hz,x,y,z);
}
int main() {
  Blob::Material free,loaded;
  tick(free,2,0,0,0);assert(free.scale[1]==1); // Elapsed time alone does nothing.
  tick(loaded,.5,0,-1.5,0);float initial=loaded.scale[1];
  tick(loaded,1.5,0,-1.5,0);float two=loaded.scale[1];
  assert(initial>.9f && two>.78f && two<.83f && loaded.scale[0]>1.09f);
  printf("Supported height after 0.5 s %.4f; after 2 s %.4f; spread %.4f / %.4f\n",initial,two,loaded.scale[0],loaded.scale[2]);
  tick(loaded,8,0,-1.5,0);assert(loaded.scale[1]>.78f && loaded.scale[1]<.82f);
  tick(loaded,10,0,0,0);assert(std::abs(loaded.scale[1]-1)<.001f);
  // The same constitutive law yields under pressure on any axis, not only weight.
  for(int axis=0;axis<3;axis++) {
    Blob::Material m;float s[3]={};s[axis]=-1.5f;tick(m,2,s[0],s[1],s[2]);
    assert(m.scale[axis]>.78f && m.scale[axis]<.83f);
    assert(std::abs(m.scale[0]*m.scale[1]*m.scale[2]-1)<.00001f);
    assert(m.memory[axis]<-.15f); // Material memory has flowed with sustained load.
    float held=m.scale[axis];m.step(1.f/30,0,0,0);assert(std::abs(m.scale[axis]-held)<.01f);
  }
  Blob::Material impact;impact.impulse(0,0,-.65f);tick(impact,.2,0,0,0);
  assert(impact.scale[2]<.95f && impact.scale[0]>1.02f);
  // Rate independence and finite, volume-preserving deformation under abuse.
  Blob::Material a,b;tick(a,2,0,-1.5f,0,30);tick(b,2,0,-1.5f,0,60);
  assert(std::abs(a.scale[1]-b.scale[1])<.0001f);
  for(int i=0;i<20000;i++) {
    a.step((i%4+1)/120.f,12*std::sin(i*.7f),-1.5f+12*std::sin(i*.5f),12*std::cos(i*.4f));
    assert(std::abs(a.scale[0]*a.scale[1]*a.scale[2]-1)<.00001f);
    for(float q:a.strain)assert(std::isfinite(q)&&std::abs(q)<=.48001f);
  }
  // Touch/inertial loads go through this material, while existing jiggle remains.
  BlobMotion::Sample motion;motion.valid=true;motion.gravity={0,-1,0};
  Blob::Physics rest,pull;
  for(int i=0;i<240;i++){rest.step(1.f/120,{},false,motion);pull.step(1.f/120,{},false,motion);}
  Blob::Touch t{true,205,290,205,290,1};pull.setImpact(1,true,0,0,0,0,.5f,1);
  for(int i=0;i<180;i++){t.y=290-int(70*std::min(1.f,i/60.f));pull.step(1.f/120,t,false,motion);}
  assert(pull.material.scale[1]>rest.material.scale[1]+.1f);
  assert(pull.material.scale[2]<rest.material.scale[2]-.1f);
  pull.step(.033f,{},true,motion);assert(pull.material.scale[1]==1 && pull.material.activity()==0);
  // Analytic normals for the complete touch + motion + material deformation.
  rest.poke.p=.14f;rest.ripple.p=-.07f;rest.pull_x.p=.2f;
  float pos[]={.1f,.3f,.5f},normal[]={0,0,1},base[3],n[3],out[3],unused[3];rest.deform(pos,normal,base,n);
  for(int axis=0;axis<2;axis++) {
    float p[]={pos[0],pos[1],pos[2]};p[axis]+=.0001f;rest.deform(p,normal,out,unused);
    float dot=0;for(int k=0;k<3;k++)dot+=(out[k]-base[k])*n[k]/.0001f;
    assert(std::abs(dot)<.003f);
  }
  puts("Force-driven creep, all-axis yielding, unloading, unit volume, normal transforms, timestep consistency, bounds, touch coupling and sleep passed.");
}
