#include "viewer/a3d_viewer.h"
#include "backends/threads/a3d_std_executor.h"
#include "blob_render.h"
#include "ditto_lighting.h"
#include "ditto_sleep_marks.h"
#include <cstdio>
#include <vector>
#include <memory>
static std::vector<uint16_t> pixels(410*502);
static BlobRenderer blob;
static Blob::Touch input;
static float seconds=0;
static int variant=0;
static bool asleep=false,speaking=false;
static float ease(float a,float b,float t){return DittoBehavior::ease(a,b,t);}
static void tile(void*,const uint16_t*p,int x,int y,int w,int h){for(int r=0;r<h;r++)memcpy(pixels.data()+(y+r)*410+x,p+r*w,w*2);}
static float tilt(float t) {
  return .40f*(ease(1.0f,1.90f,t)-ease(2.45f,3.15f,t))-.40f*(ease(3.15f,4.05f,t)-ease(4.60f,5.35f,t));
}
static float swing(float t) {
  return std::sin(6.2831853f*.85f*(t-1.05f))*ease(1.05f,1.24f,t)*(1-ease(3.15f,3.30f,t));
}
static float shake(float t) {
  return t>=1.12f&&t<3.30f?std::sin(6.2831853f*2.8f*(t-1.12f))*ease(1.12f,1.28f,t):0;
}
static BlobMotion::Sample sensor(float t) {
  // Synthetic, screen-aligned traces for animation review. They are not a
  // recorded hardware test and do not exercise the sensor fusion filter.
  BlobMotion::Sample m;m.valid=true;m.gravity={0,-1,0};m.time_us=int64_t((t+10)*1e6f);
  constexpr float eps=.002f;
  if(variant==4) {
    float a=tilt(t);m.gravity={std::sin(a),-std::cos(a),0};
    m.omega.z=-(tilt(t+eps)-tilt(t-eps))/(2*eps);
    m.alpha.z=std::clamp(-(tilt(t+eps)-2*a+tilt(t-eps))/(eps*eps),-8.f,8.f);
  } else if(variant==5) {
    float a=swing(t);m.linear={.21f*a,0,0};m.omega.z=.18f*a;
    m.alpha.z=.18f*(swing(t+eps)-swing(t-eps))/(2*eps);
    m.shake.x=.065f*a;
  } else {
    float a=shake(t);m.linear={0,.105f*a,0};m.shake={.065f*a,.29f*a,0};
    m.omega.z=.11f*a;m.alpha.z=std::clamp(.11f*(shake(t+eps)-shake(t-eps))/(2*eps),-6.f,6.f);
  }
  m.specific=m.linear-m.gravity;m.gyro=m.omega;
  return m;
}

static void pose(void*,a3d::SceneRuntime& scene,float dt) {
 auto m=sensor(seconds);if(variant<4||variant>6){m={};m.valid=true;m.gravity={0,0,-1};}
 auto g=blob.gravityInBody(scene,m.gravity);
 blob.advance(dt/1000,input,asleep,m,&g,speaking,.5f+.45f*std::sin(seconds*18));
 blob.apply(scene);
}
int main(int argc,char**argv) {
 if(argc!=5 && argc!=6)return 1;variant=atoi(argv[3]);
 a3d::Display d;d.width=410;d.height=502;d.sendTile=tile;
 a3d::StdThreadExecutor ex(argc==6?atoi(argv[5]):2);a3d::Viewer v;v.setExecutor(ex);
 a3d::ViewerConfig c;c.tileHeight=18;c.culling=-1;c.texture=a3d::TextureMode::None;
 if(!v.begin(d,c)||!v.openAssetFile(argv[1])||!blob.initialize(v.scene(),malloc))return 2;
 blob.executor=&ex;auto light=std::make_unique<a3d::SoftBackend::SurfaceMap>();auto& material=v.image().materials()[0];
 DittoLighting::build(*light,material.color[0],material.color[1],material.color[2]);
 std::vector<a3d::SoftBackend::ColorRamp> ramps(v.image().materialCount());
 for(int i=0;i<int(ramps.size());i++){auto&m=v.image().materials()[i];a3d::SoftBackend::makeSmoothColorRamp(ramps[i],m.color[0],m.color[1],m.color[2]);}
 for(int i=0;i<2;i++){v.backend(i).setSurfaceMap(light.get());v.backend(i).setColorRamps(ramps.data(),ramps.size());}
 v.setLightDirection(-.3,-.7,-1);v.setOrbit(.12,.05,3.9);v.setPoseCallback(pose,nullptr);
 v.setGeometryOverlay([](void* user,const float* vp,int w,int h){
   auto& viewer=*static_cast<a3d::Viewer*>(user);
   return blob.projectInk(viewer.scene().worldMatrix(2),vp,w,h);
 },[](void*,a3d::SoftBackend& backend,int y,int h){blob.drawInk(backend,y,h);},&v);
 v.selectClip(Avatar::Idle);blob.setEmote(Avatar::Idle,0);
 // Settle the current physical material before the controlled gestures.
 for(int i=0;i<120;i++){seconds=-4+i/30.f;v.frame(1000.f/30);}
 blob.behavior=DittoBehavior::Controller{};
 FILE*f=fopen(argv[2],"wb"),*trace=fopen(argv[4],"w");if(!f||!trace)return 3;
 fprintf(trace,"time,mode,oval,melon,gaze_x,gaze_y,eye_open,sleep,damping,shake,hit\n");
 auto press=[&](int x,int y){input={true,x,y,x,y,input.press+1};blob.pickTouch(input,v.scene(),v.binners()[0],0);if(!blob.physics.impact_hit){fprintf(stderr,"miss variant %d at %f\n",variant,seconds);exit(4);}};
 for(int i=0;i<240;i++) {
  seconds=i/30.f;
  if(variant==0||variant==7||variant==8){if(i==33)press(202,306);if(i==38)input.down=false;}
  if(variant>=1&&variant<=3){
   int k=variant-1;const int start[][2]={{242,260},{247,243},{273,222}},end[][2]={{306,207},{143,248},{296,170}};
   float begin[]={1.25f,1.14f,1.23f},finish[]={2.55f,1.38f,1.86f},release[]={2.80f,1.40f,3.70f};
   if(i==33)press(start[k][0],start[k][1]);
   if(input.press){float a=ease(begin[k],finish[k],seconds);input.x=start[k][0]+int((end[k][0]-start[k][0])*a);input.y=start[k][1]+int((end[k][1]-start[k][1])*a);}
   if(seconds>=release[k])input.down=false;
  }
  if(variant==7){if(i==51||i==69)press(202,306);if(i==56||i==74)input.down=false;}
  if(variant==8){if(i==56)press(210,270);if(i>=56){float a=ease(1.97f,2.60f,seconds);input.x=210-int(64*a);input.y=270-int(50*a);}if(seconds>=2.92f)input.down=false;}
  asleep=variant==10&&seconds>=.9f;
  if(variant==11){asleep=seconds>=.9f&&seconds<2.8f;speaking=seconds>=3.3f&&seconds<4.8f;if(i==92)press(202,306);if(i==97)input.down=false;}
  blob.setEmote(asleep?Avatar::Sleep:Avatar::Idle,seconds);
  // Mirror the approved device policy: settle, then stop animation time.
  v.frame(asleep && blob.behavior.sleep_time>=1.9f ? 0 : 1000.f/30);
  DittoSleepMarks::draw(pixels.data(),0,502,blob.behavior.sleep_time,blob.behavior.pose.sleep,0);
  auto& p=blob.behavior.pose;
  fprintf(trace,"%.4f,%s,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%d,%d\n",seconds,DittoBehavior::name(blob.behavior.mode),p.mouth.oval,p.mouth.melon,p.gaze_x,p.gaze_y,blob.eye_pose.open[0],p.sleep,blob.physics.liquid.damping_scale,blob.physics.liquid.recovery.shaking,blob.physics.impact_hit);
  for(int m=0;m<4;m++)for(unsigned n=0;n<blob.meshes[m].count*3;n++)if(!std::isfinite(blob.meshes[m].positions[n]))return 5;
  fwrite(pixels.data(),2,pixels.size(),f);
 }
 fclose(f);fclose(trace);return 0;
}
