#include "viewer/a3d_viewer.h"
#include "backends/threads/a3d_std_executor.h"
#include "blob_render.h"
#include "ditto_lighting.h"
#include <cstdio>
#include <memory>
#include <vector>
static std::vector<uint16_t> pixels(410*502);
static BlobRenderer blob;
static Blob::Touch touch;
static bool sleeping=false;
static float seconds=0;
static int clip=0;
static void tile(void*,const uint16_t* p,int x,int y,int w,int h){for(int r=0;r<h;r++)memcpy(pixels.data()+(y+r)*410+x,p+r*w,w*2);}
static void pose(void*,a3d::SceneRuntime& scene,float dt) {
  BlobMotion::Sample m;m.valid=true;m.gravity={0,0,-1};
  auto g=blob.gravityInBody(scene,m.gravity);
  blob.advance(dt/1000,touch,sleeping,m,&g);blob.apply(scene);
}
int main(int argc,char** argv) {
  if(argc<4||argc>5)return 1;clip=atoi(argv[3]);
  a3d::Display d;d.width=410;d.height=502;d.sendTile=tile;
  a3d::StdThreadExecutor ex(argc==5?atoi(argv[4]):2);a3d::Viewer v;v.setExecutor(ex);
  a3d::ViewerConfig c;c.tileHeight=18;c.culling=-1;c.texture=a3d::TextureMode::None;
  if(!v.begin(d,c)||!v.openAssetFile(argv[1])||!blob.initialize(v.scene(),malloc))return 2;
  blob.executor=&ex;
  auto light=std::make_unique<a3d::SoftBackend::SurfaceMap>();auto& material=v.image().materials()[0];
  DittoLighting::build(*light,material.color[0],material.color[1],material.color[2]);
  std::vector<a3d::SoftBackend::ColorRamp> ramps(v.image().materialCount());
  for(int i=0;i<int(ramps.size());i++){auto&m=v.image().materials()[i];a3d::SoftBackend::makeSmoothColorRamp(ramps[i],m.color[0],m.color[1],m.color[2]);}
  for(int i=0;i<2;i++){v.backend(i).setSurfaceMap(light.get());v.backend(i).setColorRamps(ramps.data(),ramps.size());}
  v.setLightDirection(-.3,-.7,-1);v.setOrbit(.12,.05,3.9);v.setPoseCallback(pose,nullptr);
  v.setGeometryOverlay([](void* user,const float* view,int w,int h){return blob.projectInk(static_cast<a3d::Viewer*>(user)->scene().worldMatrix(2),view,w,h);},
    [](void*,a3d::SoftBackend& backend,int y,int h){blob.drawInk(backend,y,h);},&v);
  v.selectClip(Avatar::Idle);
  for(int i=0;i<120;i++){blob.setEmote(Avatar::Idle,i/30.f);v.frame(1000.f/30);}
  blob.behavior=DittoBehavior::Controller{};
  v.selectClip(clip);FILE* f=fopen(argv[2],"wb");if(!f)return 3;
  float minimum=100;
  for(int i=0;i<150;i++) {
    seconds=i/30.f;int current=i<120?clip:Avatar::Idle;
    sleeping=current==Avatar::Sleep;
    if(i==120)v.selectClip(Avatar::Idle);
    blob.setEmote(current,seconds);v.frame(1000.f/30);
    for(unsigned k=0;k<blob.meshes[0].count;k++) {
      auto& mesh=blob.meshes[0];float out[3],normal[3];
      float det=blob.rig.deform(mesh.rest+k*3,mesh.rest+mesh.count*3+k*3,out,normal,blob.body_binding[k]);
      minimum=std::min(minimum,det);
      if(det<=.05f){fprintf(stderr,"Near singular skin clip %d frame %d vertex %u (%f %f %f): %f\n",clip,i,k,mesh.rest[k*3],mesh.rest[k*3+1],mesh.rest[k*3+2],det);return 6;}
    }
    for(int m=0;m<4;m++)for(unsigned n=0;n<blob.meshes[m].count*3;n++)if(!std::isfinite(blob.meshes[m].positions[n]))return 4;
    if(blob.eye_pose.width!=1 && !sleeping && i<120)return 5;
    fwrite(pixels.data(),2,pixels.size(),f);
  }
  fclose(f);fprintf(stderr,"clip %d minimum skin determinant %.6f\n",clip,minimum);return 0;
}
