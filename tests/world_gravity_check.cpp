#include "blob_render.h"
#include "viewer/a3d_viewer.h"
#include <cassert>
#include <cstdio>
using BlobMotion::V;
static V mul(const float *m,V v){return {m[0]*v.x+m[1]*v.y+m[2]*v.z,m[3]*v.x+m[4]*v.y+m[5]*v.z,m[6]*v.x+m[7]*v.y+m[8]*v.z};}
static float dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static void drive(Blob::Material &m,V g,float seconds,int hz=120){for(int i=0;i<int(seconds*hz);i++)m.step(1.f/hz,0,0,0,g);}
int main(int argc,char**argv){
 assert(argc==2);
 // Any orientation has the same axial creep, two transverse expansions and
 // positive, unit-volume deformation. Opposite gravity moves the support.
 V directions[]={{0,-1,0},{0,1,0},{-1,0,0},{1,0,0},{0,0,-1},{0,0,1},{1,1,0},{1,0,1},{0,1,1},{1,-2,3}};
 for(auto g:directions){
  g=BlobMotion::normalized(g);Blob::Material m;drive(m,g,2);
  auto axial=mul(m.matrix,g);float compression=dot(axial,g);
  assert(compression>.78f && compression<.83f);
  assert((axial-g*compression).length()<1e-5f);
  auto t=BlobMotion::normalized(BlobMotion::cross(g,std::abs(g.z)<.8f?V{0,0,1}:V{0,1,0}));
  assert(dot(mul(m.matrix,t),t)>1.09f);
  assert(std::abs(m.volume()-1)<3e-6f && dot(m.support,g)>.59f);
  float p[]={m.support.x,m.support.y,m.support.z},n[]={0,0,1};m.deform(p,n);
  assert((V{p[0],p[1],p[2]}-m.support).length()<1e-6f);
  auto identity=mul(m.normal_matrix,mul(m.matrix,{.2f,.3f,.5f}));
  assert((identity-V{.2f,.3f,.5f}).length()<2e-6f);
  // Actual deformed normal stays perpendicular to both surface tangents.
  assert(std::abs(dot(mul(m.normal_matrix,{0,0,1}),mul(m.matrix,{1,0,0})))<2e-6f);
  printf("gravity [%+.3f %+.3f %+.3f] axial %.5f volume %.7f support [%+.3f %+.3f %+.3f]\n",g.x,g.y,g.z,compression,m.volume(),m.support.x,m.support.y,m.support.z);
 }
 // A reorientation changes the load, never rotates existing strain instantly.
 Blob::Material m;drive(m,{0,-1,0},3);float before[9];std::copy(m.matrix,m.matrix+9,before);
 m.step(1.f/120,0,0,0,{1,0,0});
 for(int k=0;k<9;k++)assert(std::abs(m.matrix[k]-before[k])<.001f);
 drive(m,{1,0,0},10);assert(m.scale[0]<.81f && m.scale[1]>1.1f && m.support.x>.99f);
 drive(m,{},10);for(int k=0;k<3;k++)assert(std::abs(m.scale[k]-1)<.001f);
 // Full rotations of the device with matching accelerometer and gyro readings.
 BlobMotion::Filter f;int64_t now=1000000;f.feed({0,1,0},{},now);
 float max_error=0;
 for(int i=1;i<=1440;i++){
  float angle=i/120.f*1.5707963f;V a{std::sin(angle),std::cos(angle),0};now+=8333;
  auto s=f.feed(a,{0,0,1.5707963f},now);max_error=std::max(max_error,(s.gravity+a).length());
  m.step(1.f/120,0,0,0,s.gravity);assert(std::abs(m.volume()-1)<5e-6f);
 }
 assert(max_error<.001f);
 // Camera yaw/pitch and avatar gestures must not define physical down.
 a3d::Display display;display.width=410;display.height=502;
 display.sendTile=[](void*,const uint16_t*,int,int,int,int){};
 a3d::Viewer viewer;a3d::ViewerConfig config;assert(viewer.begin(display,config));assert(viewer.openAssetFile(argv[1]));
 auto &scene=viewer.scene();BlobRenderer blob;
 float t[]={0,0,0},r[]={0,0,0,1},scale[]={1,1,1};
 for(int k=0;k<3;k++)scene.setNodeTRS(k,t,r,scale);
 blob.view_yaw=0;blob.view_pitch=0;
 assert((blob.gravityInBody(scene,{0,-1,0})-V{0,-1,0}).length()<1e-6f);
 blob.view_pitch=1.5707963f;
 assert((blob.gravityInBody(scene,{0,-1,0})-V{0,0,1}).length()<1e-5f);
 blob.view_pitch=0;blob.view_yaw=1.5707963f;
 assert((blob.gravityInBody(scene,{0,0,-1})-V{-1,0,0}).length()<1e-5f);
 blob.view_yaw=0;blob.setEmote(Avatar::Yes,0);r[2]=std::sqrt(.5f);r[3]=std::sqrt(.5f);scene.setNodeTRS(0,t,r,scale);
 // Legacy authored root channels are discarded; the rig deforms before
 // the world-space material and cannot redefine gravity.
 assert((blob.gravityInBody(scene,{0,-1,0})-V{0,-1,0}).length()<1e-5f);
 // The approved breath and delayed attention share the exact pose transform
 // used to map world gravity. They must not quietly redefine physical down.
 assert(blob.initialize(scene,malloc));
 for(int i=0;i<60;i++) {
  r[0]=r[1]=r[2]=0;r[3]=1;for(int n=0;n<3;n++)scene.setNodeTRS(n,t,r,scale);
  blob.setEmote(Avatar::Idle,0);blob.behavior.pose.pitch=.06f*std::sin(i);
  blob.behavior.pose.roll=.07f*std::cos(i);blob.behavior.pose.breath=std::sin(i*.1f);
  blob.physics.squash.p=.12f*std::cos(i*.1f);blob.physics.yaw.p=.3f*std::sin(i*.3f);
  V input=BlobMotion::normalized(V{.3f,-.9f,.2f}),local=blob.gravityInBody(scene,input);
  blob.apply(scene);
  for(int n=2;n>=0;n--){
   float tr[3],qr[4],sc[3];scene.getNodeTRS(n,tr,qr,sc);
   local={local.x*sc[0],local.y*sc[1],local.z*sc[2]};V q{qr[0],qr[1],qr[2]};
   auto c=BlobMotion::cross(q,local);local=local+c*(2*qr[3])+BlobMotion::cross(q,c)*2;
  }
  assert((BlobMotion::normalized(local)-input).length()<1e-5f);
 }
 // Mixed forces and changing orientation exercise tensor bounds and lighting.
 for(int i=0;i<10000;i++){
  auto g=BlobMotion::normalized(V{std::sin(i*.13f),std::cos(i*.17f),std::sin(i*.29f)});
  m.step((i%4+1)/120.f,8*std::sin(i*.7f),8*std::cos(i*.4f),8*std::sin(i*.2f),g);
  assert(std::abs(m.volume()-1)<6e-6f);
  for(float value:m.matrix)assert(std::isfinite(value));
 }
 puts("Camera/pose gravity mapping and 10,000 mixed-load steps passed.");
 puts("Material directions, memory continuity, unloading, unit volume, normals and fused full rotations passed.");
}
