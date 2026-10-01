#include "viewer/a3d_viewer.h"
#include "backends/threads/a3d_std_executor.h"
#include "blob_render.h"
#include "ditto_lighting.h"
#include "ditto_listening_glow.h"
#include "ditto_damage.h"
#include <cstdio>
#include <memory>
#include <vector>
static std::vector<uint16_t> pixels(410*502);
static BlobRenderer blob;
static DittoListeningGlow glow;
static DittoDamage damage;
static void* allocate(size_t n){return malloc(n);}
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
  if(argc<3||argc>7)return 1;clip=argc>5?atoi(argv[5]):Avatar::Listening;glow.fast_background=argc<5||atoi(argv[4]);
  glow.use_cached_background=glow.fast_background;
  a3d::Display d;d.width=410;d.height=502;d.sendTile=tile;
  a3d::StdThreadExecutor ex(argc>=4?atoi(argv[3]):2);a3d::Viewer v;v.setExecutor(ex);v.setAllocator(allocate,free);
  a3d::ViewerConfig c;c.tileHeight=argc>6?atoi(argv[6]):18;c.culling=-1;c.texture=a3d::TextureMode::None;
  if(!v.begin(d,c)||!v.openAssetFile(argv[1])||!blob.initialize(v.scene(),malloc))return 2;
  blob.executor=&ex;if(!glow.begin(malloc))return 7;
  v.setBackgroundPainter([](void*,uint16_t* tile,int y,int h,int w){return glow.paintBackground(tile,y,h,w);},nullptr);
  v.setTileNeeded([](void*,int y,int h){return !glow.use_cached_background || !damage.usable ||
    (y<=damage.bounds.bottom && y+h>damage.bounds.top);},nullptr);
  auto light=std::make_unique<a3d::SoftBackend::SurfaceMap>();auto& material=v.image().materials()[0];
  DittoLighting::build(*light,material.color[0],material.color[1],material.color[2]);
  std::vector<a3d::SoftBackend::ColorRamp> ramps(v.image().materialCount());
  for(int i=0;i<int(ramps.size());i++){auto&m=v.image().materials()[i];a3d::SoftBackend::makeSmoothColorRamp(ramps[i],m.color[0],m.color[1],m.color[2]);}
  for(int i=0;i<2;i++){v.backend(i).setSurfaceMap(light.get());v.backend(i).setColorRamps(ramps.data(),ramps.size());}
  std::vector<uint16_t> hot(light->colors+256*a3d::SoftBackend::SurfaceMap::RowStride,light->colors+256*a3d::SoftBackend::SurfaceMap::RowStride+2048);
  if(glow.use_cached_background)for(int i=0;i<2;i++)v.backend(i).setSurfaceHotRow(hot.data());
  v.setLightDirection(-.3,-.7,-1);v.setOrbit(.12,.05,3.9);v.setPoseCallback(pose,nullptr);
  v.setGeometryOverlay([](void* user,const float* view,int w,int h){auto& viewer=*static_cast<a3d::Viewer*>(user);bool halo=glow.prepare(viewer.binners()[0],viewer.scene().meshIndices(0),viewer.scene().meshTriangleCount(0),h,blob.listening_amount,blob.behavior.now);bool ink=blob.projectInk(viewer.scene().worldMatrix(2),view,w,h);damage.prepare(viewer,blob,glow.level,!sleeping,glow.visible?&glow.dirty_bounds:nullptr);return ink||halo;},
    [](void*,a3d::SoftBackend& backend,int y,int h){glow.draw(backend,y,h);blob.drawInk(backend,y,h);},&v);
  v.selectClip(Avatar::Idle);
  for(int i=0;i<120;i++){blob.setEmote(Avatar::Idle,i/30.f);v.frame(1000.f/30);}
  blob.behavior=DittoBehavior::Controller{};
  v.selectClip(clip);FILE* f=fopen(argv[2],"wb");if(!f)return 3;
  float minimum=100;
  std::vector<uint16_t> previous=pixels;
  for(int i=0;i<210;i++) {
    seconds=i/30.f;int current=i>=38&&i<150?clip:Avatar::Idle;
    if(i==30&&clip==Avatar::Listening){touch={true,242,260,242,260,1};blob.pickTouch(touch,v.scene(),v.binners()[0],0);if(!blob.physics.impact_hit)return 8;}
    if(i>=38&&i<65){touch.x=242+(i-38)*2;touch.y=260-(i-38)*2;}
    if(i==150)touch.down=false;
    sleeping=current==Avatar::Sleep;
    if(i==150)v.selectClip(Avatar::Idle);
    blob.setEmote(current,seconds);v.frame(1000.f/30);
    for(unsigned k=0;k<blob.meshes[0].count;k++) {
      auto& mesh=blob.meshes[0];float out[3],normal[3];
      float det=blob.rig.deform(mesh.rest+k*3,mesh.rest+mesh.count*3+k*3,out,normal,blob.body_binding[k]);
      minimum=std::min(minimum,det);
      if(det<=.05f){fprintf(stderr,"Near singular skin clip %d frame %d vertex %u (%f %f %f): %f\n",clip,i,k,mesh.rest[k*3],mesh.rest[k*3+1],mesh.rest[k*3+2],det);return 6;}
    }
    for(int m=0;m<4;m++)for(unsigned n=0;n<blob.meshes[m].count*3;n++)if(!std::isfinite(blob.meshes[m].positions[n]))return 4;
    if(blob.eye_pose.width!=1 && !sleeping && i<150)return 5;
    if(clip==Avatar::Listening&&i>=60&&i<150&&(!blob.rig.active||blob.listening_amount<.99f||blob.physics.stretch()<.01f))return 9;
    if(i==209&&blob.listening_amount!=0)return 10;
    if(damage.usable)for(int y=0;y<502;y++)for(int x=0;x<410;x++) {
      const auto& b=damage.bounds;
      if((x<b.left||x>b.right||y<b.top||y>b.bottom)&&pixels[y*410+x]!=previous[y*410+x]) {
        fprintf(stderr,"Missed damage clip %d frame %d pixel %d,%d\n",clip,i,x,y);return 12;
      }
    }
    previous=pixels;
    fwrite(pixels.data(),2,pixels.size(),f);
  }
  fclose(f);fprintf(stderr,"clip %d minimum skin determinant %.6f\n",clip,minimum);return 0;
}
