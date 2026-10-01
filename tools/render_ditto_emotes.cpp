#include "viewer/a3d_viewer.h"
#include "backends/threads/a3d_std_executor.h"
#include "blob_render.h"
#include "ditto_lighting.h"
#include <cstdio>
#include <vector>
#include <memory>
static std::vector<uint16_t> pixels(410*502);
static BlobRenderer blob;
static Blob::Touch input;
static bool asleep=false;
static void tile(void*,const uint16_t*p,int x,int y,int w,int h){for(int r=0;r<h;r++)memcpy(pixels.data()+(y+r)*410+x,p+r*w,w*2);}
static void pose(void*,a3d::SceneRuntime &s,float dt){blob.advance(dt/1000,input,asleep);blob.apply(s);}
int main(int argc,char **argv){
 if(argc!=3 && argc!=4)return 1;
 a3d::Display d;d.width=410;d.height=502;d.sendTile=tile;
 a3d::StdThreadExecutor ex(2);a3d::Viewer v;v.setExecutor(ex);
 a3d::ViewerConfig c;c.tileHeight=20;c.culling=-1;c.texture=a3d::TextureMode::None;
 if(!v.begin(d,c)||!v.openAssetFile(argv[1])||!blob.initialize(v.scene(),malloc))return 2;
 if(argc==3)blob.executor=&ex;
 std::vector<a3d::SoftBackend::ColorRamp> ramps(v.image().materialCount());
 for(int i=0;i<int(ramps.size());i++){auto&m=v.image().materials()[i];a3d::SoftBackend::makeSmoothColorRamp(ramps[i],m.color[0],m.color[1],m.color[2]);}
 for(int i=0;i<2;i++)v.backend(i).setColorRamps(ramps.data(),ramps.size());
 auto light=std::make_unique<a3d::SoftBackend::SurfaceMap>();auto&m=v.image().materials()[0];
 DittoLighting::build(*light,m.color[0],m.color[1],m.color[2]);
 for(int i=0;i<2;i++)v.backend(i).setSurfaceMap(light.get());
 v.setLightDirection(-.3,-.7,-1);v.setOrbit(.12,.05,3.9);v.setPoseCallback(pose,nullptr);
 FILE*f=fopen(argv[2],"wb");if(!f)return 3;
 for(int clip=0;clip<Avatar::ClipCount;clip++) {
  v.selectClip(clip);
  for(int frame=0;frame<120;frame++) {
   blob.setEmote(clip,frame/30.f);
   v.frame(1000.f/30);
   // Face transform and its inverse must preserve the deformed surface point.
   for(int m=1;m<4;m++)for(unsigned n=0;n<blob.meshes[m].count;n++) {
    auto &mesh=blob.meshes[m];float p[3],q[3];BlobRenderer::transform(mesh.matrix,mesh.positions+n*3,p);
    // Rigged skin is no longer at the undeformed surface height. The shared
    // rest-space face placement is verified by the current rig renderer.
    BlobRenderer::transform(mesh.inverse,p,q);
    for(int k=0;k<3;k++)if(!std::isfinite(q[k])||std::abs(q[k]-mesh.positions[n*3+k])>.0001f)return 5;
   }
   fwrite(pixels.data(),2,pixels.size(),f);
  }
 }
 fclose(f);
}
