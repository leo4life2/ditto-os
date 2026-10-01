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
  if(t>=1 && t<5){float q=(t-1)*1.5707963f,angle=.8f*std::sin(q);a={std::sin(angle),std::cos(angle),0};w={0,0,.8f*1.5707963f*std::cos(q)};}
  if(t>=6 && t<9){float q=t-6;a={1.4f*std::sin(q*8),1+.8f*std::sin(q*6),.8f*std::sin(q*5)};}
  if(t>=10 && t<13){float q=t-10;w={2.2f*std::sin(q*5),2.2f*std::sin(q*4),3*std::sin(q*4)};}
  motion=filter.feed(a,w,1000000+int64_t(i)*33333);
  v.frame(1000.f/30);fwrite(pixels.data(),2,pixels.size(),f);
 }

 fclose(f);
}
