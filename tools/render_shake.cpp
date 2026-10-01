#include "viewer/a3d_viewer.h"
#include "backends/threads/a3d_std_executor.h"
#include "blob_render.h"
#include <cstdio>
#include <vector>
static std::vector<uint16_t> pixels(410*502);
static BlobRenderer blob;
static Blob::Touch input;
static bool asleep=false;
static BlobMotion::Sample motion;
static void tile(void*,const uint16_t*p,int x,int y,int w,int h){for(int r=0;r<h;r++)memcpy(pixels.data()+(y+r)*410+x,p+r*w,w*2);}
static void pose(void*,a3d::SceneRuntime &s,float dt){blob.physics.step(dt/1000,input,asleep,motion);blob.apply(s);}
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
 v.setLightDirection(-.3,-.7,-1);v.setOrbit(.12,.05,3.9);v.setPoseCallback(pose,nullptr);
 FILE*f=fopen(argv[2],"wb");if(!f)return 3;
 BlobMotion::Filter filter;
 for(int i=0;i<480;i++){
  float t=i/30.f;BlobMotion::V a{0,1,0},w{};
  for(int tick=0;tick<2;tick++) {
    float q=(i*2+tick)/60.f; a={0,1,0};
    if(q>=1 && q<3)a.x=2*std::sin((q-1)*25.132741f);
    if(q>=5 && q<7)a.y+=2*std::sin((q-5)*31.415926f);
    if(q>=9 && q<11)a.z=2*std::sin((q-9)*25.132741f);
    if(q>=13 && q<14.5f){a.x=1.4f*std::sin((q-13)*31.415926f);a.y+=1.4f*std::cos((q-13)*25.132741f);a.z=1.2f*std::sin((q-13)*28.274334f);}
    motion=filter.feed(a,w,1000000+int64_t(i*2+tick)*16667);
  }
  v.frame(1000.f/30);fwrite(pixels.data(),2,pixels.size(),f);
 }

 fclose(f);
}
