#pragma once
#include "scene_damage.h"
#include "blob_render.h"
#include "viewer/a3d_viewer.h"

// Native chrome is immutable between its explicit changes; glow bounds track
// the translated upper-body stamp alongside the character.
// Only projected geometry and procedural ink can then change. Include both
// current and preceding bounds so the old silhouette is always erased.
struct DittoDamage {
  SceneDamageBounds bounds,previous;
  bool usable=false,previous_eligible=false;
  int previous_level=-1;
  void prepare(a3d::Viewer& viewer,const BlobRenderer& blob,int level,bool eligible,
               const SceneDamageBounds* effect=nullptr) {
    SceneDamageBounds current;
    // Reduce in float space, then round outwards just once. Calling floor/
    // ceil for every vertex is particularly expensive on the S3.
    float left=1e9f,right=-1e9f,top=1e9f,bottom=-1e9f;
    auto point=[&](float x,float y,float margin=2.f) {
      if(!std::isfinite(x+y+margin)){current.valid=false;return;}
      left=std::min(left,x-margin);right=std::max(right,x+margin);
      top=std::min(top,y-margin);bottom=std::max(bottom,y+margin);
    };
    if(viewer.drawableCount()!=4)current.valid=false;
    else for(int m=0;m<4;m++) {
      const auto& b=viewer.binners()[m];const auto* x=b.screenX();const auto* y=b.screenY();const auto* behind=b.behind();
      for(unsigned i=0;i<blob.meshes[m].count;i++) {
        if(behind[i]){current.valid=false;continue;}
        point(x[i],y[i]);
      }
    }
    const auto& ink=blob.dizzy_ink;
    for(int part=0;part<3;part++)if((part<2?ink.amount:ink.mouth_amount)>.001f) {
      const auto* p=ink.strokes+(part<2?part*(ink.Steps+1):2*(ink.Steps+1));
      const int n=part<2?ink.Steps+1:ink.MouthVertices+1;
      for(int i=0;i<n;i++)if(p[i].iw>0)point(p[i].x,p[i].y,p[i].radius+2);
    }
    const auto& star=blob.wink_sparkle;
    if(star.visible)point(star.cx,star.cy,star.radius*1.35f+2);
    if(left<=right && top<=bottom){current.point(left,top,0);current.point(right,bottom,0);}
    if(effect)current.unite(*effect);
    eligible&=blob.behavior.pose.sleep<=.001f;
    bounds=current;bounds.unite(previous);
    usable=eligible&&previous_eligible&&level==previous_level&&bounds.valid;
    previous=current;previous_eligible=eligible;previous_level=level;
  }
};
