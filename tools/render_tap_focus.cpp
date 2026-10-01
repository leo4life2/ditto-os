#include "viewer/a3d_viewer.h"
#include "backends/threads/a3d_std_executor.h"
#include "blob_render.h"
#include <cstdio>
#include <vector>
static std::vector<uint16_t> pixels(410*502);
static BlobRenderer blob;
static Blob::Touch input;
static bool asleep=false;
static void tile(void*,const uint16_t*p,int x,int y,int w,int h){for(int r=0;r<h;r++)memcpy(pixels.data()+(y+r)*410+x,p+r*w,w*2);}
static void pose(void*,a3d::SceneRuntime &s,float dt){blob.physics.step(dt/1000,input,asleep);blob.apply(s);}
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
 const int points[][2]={{105,240},{205,150},{220,285},{300,265}};
 for(int i=0;i<360;i++){
  int phase=i%90,which=i/90;
  if(phase==20)input={true,points[which][0],points[which][1],points[which][0],points[which][1],uint32_t(which+1)};
  if(phase==23)input.down=false;
  if(i && input.press!=blob.physics.seen_press)blob.pickTouch(input,v.scene(),v.binners()[0],0);
  v.frame(1000.f/30);fwrite(pixels.data(),2,pixels.size(),f);
  if(phase==24 && !blob.physics.impact_hit)return 4;
 }

 fclose(f);
}
