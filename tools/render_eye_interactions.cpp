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
static int clip=Avatar::Idle;
static float seconds=0;
static void tile(void*,const uint16_t*p,int x,int y,int w,int h){for(int r=0;r<h;r++)memcpy(pixels.data()+(y+r)*410+x,p+r*w,w*2);}
static void pose(void*,a3d::SceneRuntime&s,float dt){
 BlobMotion::Sample motion;motion.valid=true;motion.gravity={.06f,-.20f,-.978f};
 auto g=blob.gravityInBody(s,motion.gravity);
 blob.setEmote(clip,seconds);blob.advance(dt/1000,input,false,motion,&g);blob.apply(s);
}
int main(int argc,char**argv){
 if(argc!=3 && argc!=4)return 1;
 a3d::Display d;d.width=410;d.height=502;d.sendTile=tile;
 #ifndef SCOUT_RENDER_WORKERS
 #define SCOUT_RENDER_WORKERS 2
 #endif
 #ifndef SCOUT_RENDER_TILE_ROWS
 #define SCOUT_RENDER_TILE_ROWS 18
 #endif
 a3d::StdThreadExecutor ex(SCOUT_RENDER_WORKERS);a3d::Viewer v;v.setExecutor(ex);
 a3d::ViewerConfig c;c.tileHeight=SCOUT_RENDER_TILE_ROWS;c.culling=-1;c.texture=a3d::TextureMode::None;
 if(!v.begin(d,c)||!v.openAssetFile(argv[1])||!blob.initialize(v.scene(),malloc))return 2;
 blob.executor=&ex;std::vector<a3d::SoftBackend::ColorRamp> ramps(v.image().materialCount());
 for(int i=0;i<int(ramps.size());i++){auto&m=v.image().materials()[i];a3d::SoftBackend::makeSmoothColorRamp(ramps[i],m.color[0],m.color[1],m.color[2]);}
 for(int i=0;i<SCOUT_RENDER_WORKERS;i++)v.backend(i).setColorRamps(ramps.data(),ramps.size());
 #ifndef SCOUT_LEGACY_LIGHTING
 auto light=std::make_unique<a3d::SoftBackend::SurfaceMap>();auto&m=v.image().materials()[0];
 DittoLighting::build(*light,m.color[0],m.color[1],m.color[2]);
 for(int i=0;i<SCOUT_RENDER_WORKERS;i++)v.backend(i).setSurfaceMap(light.get());

 #endif
 v.setLightDirection(-.3,-.7,-1);v.setOrbit(.12,.05,3.9);v.setPoseCallback(pose,nullptr);
 FILE*f=fopen(argv[2],"wb");if(!f)return 3;
 FILE*body=argc==4?fopen(argv[3],"wb"):nullptr;if(argc==4&&!body)return 4;
 for(int i=0;i<480;i++){
  seconds=i/30.f;
  if(i==90)input={true,205,250,205,250,1};
  if(i>=90&&i<102)input.x=205-(i-90)*3;
  if(i==102)input.down=false;
  if(i==180)input={true,205,250,205,250,2};
  if(i>=180&&i<192)input.x=205+(i-180)*3;
  if(i==192)input.down=false;
  if(i==255)input={true,205,250,205,250,3};
  if(i>=255&&i<285){input.x=205+(i-255)*3;input.y=250-(i-255);}
  if(i==315)input.down=false;
  if(i && input.press!=blob.physics.seen_press)blob.pickTouch(input,v.scene(),v.binners()[0],0);
  #ifdef SCOUT_LIGHTING_FADE_TEST
  // Exercise both pre-quantized UI fades and the legacy packed attenuation.
  // These are immutable for each frame and identical across both workers.
  uint16_t smooth[502];uint8_t coarse[502];
  for(int y=0;y<502;y++){smooth[y]=(y*7+i)%257;coarse[y]=(y+i)%33;}
  for(int worker=0;worker<SCOUT_RENDER_WORKERS;worker++) {
   v.backend(worker).setSmoothRowFade(i>=320?smooth:nullptr);
   v.backend(worker).setRowFade(i>=400?coarse:nullptr);
  }
  #endif
  v.frame(1000.f/30);fwrite(pixels.data(),2,pixels.size(),f);
  if(body){
   fwrite(blob.meshes[0].positions,sizeof(float),blob.meshes[0].count*3,body);
   fwrite(blob.meshes[0].normals,sizeof(float),blob.meshes[0].count*3,body);
   for(int n=0;n<3;n++){float t[3],r[4],s[3];v.scene().getNodeTRS(n,t,r,s);fwrite(t,4,3,body);fwrite(r,4,4,body);fwrite(s,4,3,body);}
  }
 }
 fclose(f);
 if(body)fclose(body);
}
