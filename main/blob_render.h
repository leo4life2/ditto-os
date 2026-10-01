#pragma once
#include "blob_physics.h"
#include "scene_layout.h"
#include "blob_pick.h"
#include "avatar_asset.h"
#include "ditto_face.h"
#include "ditto_eyes.h"
#include "ditto_behavior.h"
#include "ditto_dizzy_ink.h"
#include "ditto_wink_sparkle.h"
#include "ditto_rig.h"
#include "a3d/a3d_runtime.h"
#include <cstring>

// The same deformation code is used by firmware and desktop visual QA.
struct BlobRenderer {
  struct Mesh { float *rest=nullptr, *positions=nullptr, *normals=nullptr; uint32_t count=0; float offset[3]={}; float matrix[16]={}, inverse[16]={}; } meshes[4];
  Blob::Physics physics;
  DittoRig::Skeleton rig;
  DittoRig::Binding* body_binding=nullptr;
  a3d::IJobExecutor *executor=nullptr;
  int vertices=0;
  float view_yaw=.12f,view_pitch=.05f;
  float eye_seconds=0;
  bool eye_sleeping=false;
  DittoEyes::Controller eyes;
  DittoEyes::Pose eye_pose;
  DittoBehavior::Controller behavior;
  DittoBehavior::Pose presentation_face;
  bool requested_reaction=false,reaction_face=false;
  DittoDizzyInk dizzy_ink;
  DittoWinkSparkle wink_sparkle;
  int emote=Avatar::Idle;
  bool physical_sleep=false,physics_parked=false;
  float listening_amount=0;
  bool advance(float dt,const Blob::Touch& touch,bool sleeping,const BlobMotion::Sample& motion={},const BlobMotion::V* gravity=nullptr,bool speaking=false,float speech=0) {
    bool stepped=false;
    if(!sleeping) {physics.step(dt,touch,false,motion,gravity);stepped=true;physics_parked=false;}
    else physics.seen_press=touch.press;
    behavior.step(dt,touch,physics.impact_hit,motion,physics.liquid,sleeping||eye_sleeping,speaking,speech);
    if(sleeping && behavior.pose.sleep>=.999f && !physics_parked){physics.reset();physics_parked=true;}
    const bool listening=emote==Avatar::Listening&&!sleeping&&!eye_sleeping;
    listening_amount+=((listening?1.f:0.f)-listening_amount)*(1-std::exp(-std::clamp(dt,0.f,.1f)*10));
    if(listening_amount<.001f)listening_amount=0;
    // A confirmed remote action owns the authored face/rig after release.
    // The old contact-recovery mouth lasts almost four seconds; waiting for
    // quiet would consume the entire emote without moving a single joint.
    // Keep that behavior state and the liquid integrator running underneath.
    reaction_face=requested_reaction&&emote!=Avatar::Idle&&!touch.down&&!sleeping&&!eye_sleeping&&!speaking;
    presentation_face=behavior.pose;
    if(reaction_face) {
      presentation_face.quiet=true;presentation_face.mouth={};
      presentation_face.dizzy=0;presentation_face.blink=0;
      presentation_face.gaze_x=presentation_face.gaze_y=0;
      presentation_face.auto_blink=true;
    }
    rig.step(dt,emote,eye_seconds,!sleeping&&!eye_sleeping&&
        (reaction_face||(behavior.pose.dizzy==0&&
         (listening||behavior.pose.quiet||(speaking&&behavior.mode==DittoBehavior::Mode::Idle)))));
    physical_sleep=sleeping;return stepped;
  }
  static float smooth(float x) {x=std::clamp(x,0.f,1.f);return x*x*(3-2*x);}
  void setEmote(int clip,float seconds,bool confirmed_reaction=false) {
    emote=clip;eye_seconds=seconds;eye_sleeping=clip==Avatar::Sleep;
    requested_reaction=confirmed_reaction;
  }
  static void transform(const float *m,const float *p,float *out) {
    for(int k=0;k<3;k++)out[k]=m[k]*p[0]+m[4+k]*p[1]+m[8+k]*p[2]+m[12+k];
  }
  BlobMotion::V gravityInBody(a3d::SceneRuntime &scene,BlobMotion::V g) const {
    // Inverse of the viewer's orbit basis: measured gravity is in screen
    // coordinates, while the material deforms the mesh before its pose.
    float cy=std::cos(view_yaw),sy=std::sin(view_yaw),cp=std::cos(view_pitch),sp=std::sin(view_pitch);
    BlobMotion::V v{cy*g.x-sy*sp*g.y+sy*cp*g.z,cp*g.y+sp*g.z,-sy*g.x-cy*sp*g.y+cy*cp*g.z};
    for(int node=0;node<=2;node++) {
      float t[3]={},r[4]={0,0,0,1},s[3]={1,1,1};scene.getNodeTRS(node,t,r,s);
      if(node==0) {
        r[0]=r[1]=r[2]=0;r[3]=1;
        float q[4],out[4];rootRotation(q);a3d::quatMul(q,r,out);std::copy(out,out+4,r);
      }
      if(node==1) {
        s[0]=s[1]=s[2]=1;breathScale(s);
        squashScale(s);
      }
      BlobMotion::V q{-r[0],-r[1],-r[2]};
      auto c=BlobMotion::cross(q,v);v=v+c*(2*r[3])+BlobMotion::cross(q,c)*2;
      v={v.x/s[0],v.y/s[1],v.z/s[2]};
    }
    return v.length()>1e-6f?BlobMotion::normalized(v)*g.length():BlobMotion::V{};
  }
  void rootRotation(float *q) const {
    float awake=1-behavior.pose.sleep;
    float hy=physics.yaw.p*.5f*awake,hz=(physics.roll.p+physics.bounce_roll.p)*.5f*awake;
    q[0]=std::sin(hy)*std::sin(hz);q[1]=std::sin(hy)*std::cos(hz);
    q[2]=std::cos(hy)*std::sin(hz);q[3]=std::cos(hy)*std::cos(hz);
    const auto& face=behavior.pose;
    float attention[4]={std::sin(face.pitch*.5f)*std::cos(face.roll*.5f),-std::sin(face.pitch*.5f)*std::sin(face.roll*.5f),std::cos(face.pitch*.5f)*std::sin(face.roll*.5f),std::cos(face.pitch*.5f)*std::cos(face.roll*.5f)};
    float out[4];a3d::quatMul(q,attention,out);std::copy(out,out+4,q);
  }
  void breathScale(float *s) const {
    const auto& face=behavior.pose;
    float amplitude=face.sleep>0?.008f-.002f*face.sleep:(face.quiet?.008f:.002f);
    s[1]=1+amplitude*face.breath;s[0]=s[2]=1/std::sqrt(s[1]);
  }
  void squashScale(float *s) const {
    float awake=1-behavior.pose.sleep,vertical=1+physics.squash.p*awake;
    s[1]*=vertical;s[0]/=std::sqrt(vertical);s[2]/=std::sqrt(vertical);
    s[0]*=1+std::max(0.f,physics.dent.p)*.16f*awake;
    s[1]*=1+std::max(0.f,physics.dent.p)*.08f*awake;
  }
  bool initialize(a3d::SceneRuntime &scene, void *(*allocate)(size_t)) {
    if(scene.meshCount()!=4 || scene.nodeCount()!=6) return false;
    if(scene.meshVertexCount(1)!=21 || scene.meshVertexCount(2)!=21 || scene.meshVertexCount(3)!=50) return false;
    if(!dizzy_ink.begin(allocate))return false;
    for(unsigned m=0;m<4;m++) {
      auto &mesh=meshes[m]; mesh.count=scene.meshVertexCount(m); vertices+=mesh.count;
      mesh.positions=scene.mutableMeshPositions(m); mesh.normals=scene.mutableMeshNormals(m);
      mesh.rest=(float*)allocate(mesh.count*6*sizeof(float));
      if(!mesh.rest || !scene.mutableMeshPositions(m) || !scene.mutableMeshNormals(m)) return false;
      memcpy(mesh.rest,scene.mutableMeshPositions(m),mesh.count*3*sizeof(float));
      memcpy(mesh.rest+mesh.count*3,scene.mutableMeshNormals(m),mesh.count*3*sizeof(float));
      float r[4],s[3]; scene.getNodeTRS(m+2,mesh.offset,r,s);
      mesh.offset[1]-=.82f;
    }
    body_binding=(DittoRig::Binding*)allocate(meshes[0].count*sizeof(DittoRig::Binding));
    if(!body_binding)return false;
    for(unsigned v=0;v<meshes[0].count;v++)body_binding[v]=DittoRig::bind(meshes[0].rest+v*3);
    return true;
  }
  void pickTouch(const Blob::Touch &touch,a3d::SceneRuntime &scene,const a3d::TileBinner &b,int y_offset=SceneLayout::StandardOffset) {
    auto hit=Blob::pick(b,scene.meshIndices(0),scene.meshTriangleCount(0),meshes[0].rest,meshes[0].normals,float(touch.press_x),float(touch.press_y-y_offset));
    if(y_offset && (touch.press_y<SceneLayout::Top || touch.press_y>=SceneLayout::Bottom))hit.hit=false;
    if(hit.hit&&rig.active) {
      float point[]={hit.x,hit.y,hit.z},normal[]={hit.nx,hit.ny,hit.nz},bound[3],bound_normal[3];
      rig.deform(point,normal,bound,bound_normal);
      hit.x=bound[0];hit.y=bound[1];hit.z=bound[2];
      hit.nx=bound_normal[0];hit.ny=bound_normal[1];hit.nz=bound_normal[2];
    }
    physics.setImpact(touch.press,hit.hit,hit.x,hit.y,hit.nx,hit.ny,hit.z,hit.nz);
  }
  void apply(a3d::SceneRuntime &scene) {
    float t[3],r[4],s[3],q[4],out[4];
    const auto& face=presentation_face;
    float awake=1-face.sleep;
    // The bone rig owns authored movement. The asset still supplies mesh,
    // bind placement and semantic clip durations, never a second body motion.
    scene.getNodeTRS(0,t,r,s);t[0]=t[1]=t[2]=0;r[0]=r[1]=r[2]=0;r[3]=1;s[0]=s[1]=s[2]=1;
    scene.setNodeTRS(0,t,r,s);
    scene.getNodeTRS(1,t,r,s);s[0]=s[1]=s[2]=1;breathScale(s);scene.setNodeTRS(1,t,r,s);
    scene.getNodeTRS(0,t,r,s);
    t[0]+=(physics.x.p+physics.bounce_x.p)*awake;t[1]+=(physics.y.p+physics.bounce_y.p)*awake;
    t[2]+=physics.bounce_z.p*awake;
    rootRotation(q);
    a3d::quatMul(q,r,out); scene.setNodeTRS(0,t,out,s);
    scene.getNodeTRS(1,t,r,s);
    squashScale(s);
    scene.setNodeTRS(1,t,r,s);
    eye_pose=eyes.resolvePose(behavior.now,face.sleep,face.quiet?rig.pose.wink:0,face.blink,
                             face.auto_blink,face.gaze_x+(face.quiet?rig.pose.gaze_x:0),face.gaze_y+(face.quiet?rig.pose.gaze_y:0),face.dizzy);
    for(int node=3;node<=5;node++) {
      scene.getNodeTRS(node,t,r,s);
      const float identity[4]={0,0,0,1};
      // One final geometry owner; neither clip transforms nor UI moods can
      // independently distort this eye/mouth pose.
      s[0]=s[1]=s[2]=1;std::copy(identity,identity+4,out);
      scene.setNodeTRS(node,t,out,s);
      auto &mesh=meshes[node-2];float local_t[3]={t[0],t[1]-.82f,t[2]};
      a3d::mat4FromTRS(local_t,out,s,mesh.matrix);
      std::fill(mesh.inverse,mesh.inverse+16,0.f);mesh.inverse[15]=1;
      for(int row=0;row<3;row++)for(int col=0;col<3;col++)
        mesh.inverse[col*4+row]=mesh.matrix[row*4+col]/(s[row]*s[row]);
      for(int row=0;row<3;row++)for(int col=0;col<3;col++)
        mesh.inverse[12+row]-=mesh.inverse[col*4+row]*local_t[col];
    }
    if(executor) executor->parallelFor(vertices, &deform_range, this, 128);
    else deform_range(this,0,vertices);
    const float centers[2][2]={{meshes[1].matrix[12]+eye_pose.gaze,meshes[1].matrix[13]+eye_pose.gaze_y},
                              {meshes[2].matrix[12]+eye_pose.gaze,meshes[2].matrix[13]+eye_pose.gaze_y}};
    dizzy_ink.pose(eye_pose.spiral,behavior.now,centers,[&](const float* source,float* dest){
      float normal[3]={0,0,1},out_normal[3],bound[3],bound_normal[3];
      rig.deform(source,normal,bound,bound_normal);physics.deform(bound,bound_normal,dest,out_normal);
    });
    const float open=std::max({face.mouth.oval,face.mouth.melon,
                             face.quiet?rig.pose.open:0.f,face.sleep,face.dizzy});
    dizzy_ink.poseMouth(smooth(open/.18f),[&](int v,float* dest){
      float local[3],source[3],normal[3]={0,0,1},bound[3],bound_normal[3],out_normal[3];
      mouthPoint(v,local,true);transform(meshes[3].matrix,local,source);
      source[2]=DittoFace::surface(source[0],source[1])+.045f;
      rig.deform(source,normal,bound,bound_normal);
      physics.deform(bound,bound_normal,dest,out_normal);
      if(physical_sleep)for(int k=0;k<3;k++)dest[k]+=(bound[k]-dest[k])*face.sleep;
    });
    wink_sparkle.pose(emote==Avatar::Wink&&face.quiet&&face.sleep<=.001f&&face.dizzy<=.001f&&
                       !physical_sleep&&!eye_sleeping,eye_seconds,centers[0],[&](const float* source,float* dest){
      float normal[3]={0,0,1},out_normal[3],bound[3],bound_normal[3];
      rig.deform(source,normal,bound,bound_normal);physics.deform(bound,bound_normal,dest,out_normal);
    });
  }
  bool projectInk(const float* model,const float* view,int width,int height) {
    bool dizzy=dizzy_ink.project(model,view,width,height);
    bool wink=wink_sparkle.project(model,view,width,height);
    return dizzy||wink;
  }
  void drawInk(a3d::SoftBackend& renderer,int top,int rows) const {
    dizzy_ink.draw(renderer,top,rows);wink_sparkle.draw(renderer,top,rows);
  }
  // One mouth contour for the dark underlay and pink skin-following fill.
  // The existing dizzy interior retains its slightly inset wavy silhouette.
  void mouthPoint(int v,float* source,bool interior) const {
    std::copy(meshes[3].rest+v*3,meshes[3].rest+v*3+3,source);
    const auto& face=presentation_face;
    const float u=-1.f+2.f*float(v/2)/24.f;
    const float arch=std::sqrt(std::max(0.f,1-u*u));
    const float ox=.125f*u,oy=(v%2?1.f:-1.f)*.0417f*arch;
    const auto& round=DittoFace::RoundMouth[v];
    float melon=std::max(face.mouth.melon,face.quiet?rig.pose.open:0.f);
    float oval=face.mouth.oval;
    source[0]+=oval*(ox-source[0])+melon*.66f*(round.x-source[0]);
    source[1]+=oval*(oy-source[1])+melon*.66f*(round.y-source[1]);
    // Downbeat authored reactions keep a closed, softly wavy ink line.
    // Physical interaction and voice mouth poses retain their priority.
    float squiggle=face.quiet?rig.pose.squiggle:0.f;
    if(squiggle>0) {
      constexpr float pi=3.14159265f;
      float x=.18f*u,y=.018f*std::sin(2*pi*u),slope=.018f*2*pi/.18f*std::cos(2*pi*u);
      float width=(v%2?1.f:-1.f)*.009f/std::sqrt(1+slope*slope);
      source[0]+=(x-width*slope-source[0])*squiggle;
      source[1]+=(y+width-source[1])*squiggle;
    }
    // A dark wavy outline; the shared geometry overlay supplies its
    // inset pink interior, seated on the same deforming skin.
    if(face.dizzy>0) {
      float wx,wy;DittoDizzyInk::mouth(u,v%2,behavior.now,interior,wx,wy);
      // Keep the old dark mesh inside the pink fill; the overlay owns
      // the smooth outline so no aliased black edge shows underneath.
      if(!interior){wx*=.90f;wy*=.80f;}
      source[0]+=(wx-source[0])*face.dizzy;source[1]+=(wy-source[1])*face.dizzy;
    }
    float sx=.029f*u,sy=(v%2?1.f:-1.f)*(.023f+.003f*face.breath)*arch;
    source[0]+=(sx-source[0])*face.sleep;source[1]+=(sy-source[1])*face.sleep;
  }
  static void deform_range(void *context,int begin,int end) {
    auto &self=*static_cast<BlobRenderer*>(context);
    int base=0;
    for(unsigned m=0;m<4;m++) {
      const auto &mesh=self.meshes[m];
      const int first=std::max(0,begin-base),last=std::min(int(mesh.count),end-base);
      for(int v=first;v<last;v++) {
        float source[3],dest[3];
        for(int k=0;k<3;k++)source[k]=mesh.rest[v*3+k];
        if(m==1||m==2) {
          // The spiral replaces the dot instead of drawing around a large
          // black pupil. Gaze and the ordinary dot return during recovery.
          float dot=1-self.eye_pose.spiral;
          if(m==1&&self.eye_pose.chevron>0) {
            source[0]=DittoEyes::Chevron[v][0];
            source[1]=DittoEyes::Chevron[v][1];
          }
          source[0]=source[0]*self.eye_pose.width*dot+self.eye_pose.gaze;
          source[1]=source[1]*self.eye_pose.open[m-1]*dot+self.eye_pose.gaze_y;
        } else if(m==3) {
          self.mouthPoint(v,source,false);
        }
        float normal[3];for(int k=0;k<3;k++)normal[k]=mesh.rest[mesh.count*3+v*3+k];
        if(m) {
          float local[3]={source[0],source[1],source[2]};
          transform(mesh.matrix,local,source);
          // Re-seat ink after blink/tilt transforms on the actual skin surface.
          source[2]=DittoFace::surface(source[0],source[1])+.030f;
        }
        // Skin the entire continuous surface, including its ink, before the
        // unchanged liquid deformation. Body rest weights are immutable.
        if(self.rig.active) {
          float bound[3],bound_normal[3];
          auto binding=m?DittoRig::bind(source):self.body_binding[v];
          self.rig.deform(source,normal,bound,bound_normal,binding);
          std::copy(bound,bound+3,source);std::copy(bound_normal,bound_normal+3,normal);
        }
        self.physics.deform(source,normal,dest,mesh.normals+v*3);
        if(self.physical_sleep) {
          float f=self.behavior.pose.sleep;
          for(int k=0;k<3;k++){dest[k]+=(source[k]-dest[k])*f;mesh.normals[v*3+k]+=(normal[k]-mesh.normals[v*3+k])*f;}
        }
        if(m)transform(mesh.inverse,dest,mesh.positions+v*3);
        else for(int k=0;k<3;k++)mesh.positions[v*3+k]=dest[k];
      }
      base+=mesh.count;
    }
  }
};
