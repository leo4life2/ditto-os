#include "viewer/a3d_viewer.h"
#include "blob_render.h"
#include <cassert>
#include <cstdio>
static BlobRenderer blob;
static void pose(void*,a3d::SceneRuntime&s,float){
 // Deliberately supply conflicting legacy authored eye transforms. Only the
 // central eye pose should survive, including during an emote transition.
 for(int n=3;n<=4;n++){
  float t[3],r[4],scale[3];s.getNodeTRS(n,t,r,scale);
  scale[0]=.8f;scale[1]*=.35f;r[2]=.15f;r[3]=std::sqrt(1-r[2]*r[2]);
  s.setNodeTRS(n,t,r,scale);
 }
 blob.apply(s);
}
int main(int argc,char**argv){
 assert(argc==2);a3d::Display d;d.width=410;d.height=502;d.sendTile=[](void*,const uint16_t*,int,int,int,int){};
 a3d::Viewer v;a3d::ViewerConfig c;c.texture=a3d::TextureMode::None;
 assert(v.begin(d,c)&&v.openAssetFile(argv[1])&&blob.initialize(v.scene(),malloc));v.setPoseCallback(pose,nullptr);
 int checked=0;
 for(int clip=0;clip<Avatar::ClipCount;clip++)for(float time:{.3f,1.6f,2.65f,2.72f,3.2f})for(float gaze:{-1.f,1.f}){
  blob.physics.side_eye=1;blob.physics.gaze=gaze;blob.physics.x_eyes=1;
  blob.setEmote(clip,time);blob.behavior.now=time;
  blob.behavior.pose.gaze_x=.045f*gaze;blob.behavior.pose.gaze_y=-.02f;
  blob.behavior.pose.sleep=clip==Avatar::Sleep?1.f:0.f;
  v.selectClip(clip);v.seek(time*1000);v.frame(0);
  for(int eye=0;eye<2;eye++){
   auto &mesh=blob.meshes[eye+1];float t[3],r[4],s[3];v.scene().getNodeTRS(eye+3,t,r,s);
   assert(s[0]==1 && s[2]==1 && s[1]==1 && blob.eye_pose.cross==0);
   assert(r[0]==0 && r[1]==0 && r[2]==0 && r[3]==1);
   for(unsigned i=0;i<mesh.count;i++){
    // Only the coordinated pose may change the local dot silhouette. Legacy
    // morphs and authored transforms cannot add another flattening or curve.
    assert(std::abs(mesh.positions[3*i]-mesh.rest[3*i]*blob.eye_pose.width-blob.eye_pose.gaze)<2e-5f);
    assert(std::abs(mesh.positions[3*i+1]-mesh.rest[3*i+1]*blob.eye_pose.open[eye]-blob.eye_pose.gaze_y)<2e-5f);
   }
  }
  checked++;
 }
 printf("Dot silhouette invariant in %d emote/time/gaze combinations despite conflicting legacy eye transforms.\n",checked);
}
