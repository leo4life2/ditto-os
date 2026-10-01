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
 for(int i=0;i<400;i++){
  if(i==10)input={true,120,275,120,275,1};if(i==13)input.down=false;
  if(i==45)input={true,205,210,205,210,2};
  if(i>=45&&i<76){input.x=205;input.y=210+(i-45)*3;}
  if(i==90)input.down=false;
  if(i==125)input={true,220,250,220,250,3};
  if(i>=125&&i<156){input.x=220+(i-125)*3;input.y=250-(i-125);}
  if(i==170)input.down=false;
  if(i==205)input={true,270,290,270,290,4};
  if(i>=205&&i<265){float a=(i-205)*.17f;input.x=205+65*cosf(a);input.y=290-65*sinf(a);}
  if(i==265)input.down=false;
  if(i==350){asleep=true;v.selectClip(9);}
  if(i && input.press!=blob.physics.seen_press)blob.pickTouch(input,v.scene(),v.binners()[0],0);
  v.frame(1000.f/30);fwrite(pixels.data(),2,pixels.size(),f);
 }
 fclose(f);
}
