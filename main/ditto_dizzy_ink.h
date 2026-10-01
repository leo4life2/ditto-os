#pragma once
#include "backends/soft/a3d_soft_backend.h"
#include "ditto_face.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Evenly spaced spiral ribbons and the shared pink open-mouth fill/outline.
// Each has independent visibility. All follow the deforming skin and share
// its depth buffer, so they cannot show through the body.
struct DittoDizzyInk {
  static constexpr int Steps=64,Rings=Steps+1,PerEye=Rings*2;
  static constexpr int MouthSteps=24,MouthVertices=50;
  static constexpr int Vertices=PerEye*2+MouthVertices,Triangles=MouthSteps*2,StrokePoints=2*(Steps+1)+MouthVertices+1;
  float *rest=nullptr,*positions=nullptr,*sx=nullptr,*sy=nullptr,*sw=nullptr;
  a3d::SoftBackend::StrokePoint* strokes=nullptr;
  static constexpr int MaxRows=512;
  uint32_t* row_masks=nullptr;
  bool binned=false;
  uint16_t* indices=nullptr;
  uint8_t* behind=nullptr;
  float amount=0,mouth_amount=0,min_y[3]={},max_y[3]={};
  static void mouth(float u,bool upper,float seconds,bool inset,float& x,float& y) {
    u=std::sin(u*1.57079633f);
    const float u2=u*u,u4=u2*u2;
    const float arch=std::sqrt(std::max(0.f,1-u4*u4));
    x=(inset?.243f:.255f)*u;
    // Broad, open cartoon shape with a gentle ripple on both lips.
    const float wave=.014f*std::sin(u*6.2831853f+.20f*std::sin(seconds*1.7f));
    y=wave+(upper?1.f:-1.f)*(inset?.052f:.063f)*arch;
  }
  bool begin(void*(*allocate)(size_t)) {
    auto* data=static_cast<float*>(allocate(Vertices*8*sizeof(float)+StrokePoints*sizeof(a3d::SoftBackend::StrokePoint)+3*MaxRows*2*sizeof(uint32_t)+Triangles*3*sizeof(uint16_t)+Vertices));
    if(!data)return false;
    rest=data;positions=rest+Vertices*2;sx=positions+Vertices*3;sy=sx+Vertices;sw=sy+Vertices;
    strokes=reinterpret_cast<a3d::SoftBackend::StrokePoint*>(sw+Vertices);
    row_masks=reinterpret_cast<uint32_t*>(strokes+StrokePoints);
    indices=reinterpret_cast<uint16_t*>(row_masks+3*MaxRows*2);behind=reinterpret_cast<uint8_t*>(indices+Triangles*3);
    for(int eye=0;eye<2;eye++)for(int ring=0;ring<Rings;ring++) {
      const int i=ring;
      float t=std::clamp(float(i)/Steps,0.f,1.f),turn=(eye?-1.f:1.f)*10.3672558f;
      // Archimedean spiral: constant radius gained per angle, 1.65 turns.
      float a=t*turn,r=.112f*t,c=std::cos(a),s=std::sin(a);
      float dx=.112f*c-r*turn*s,dy=.112f*s+r*turn*c;
      float inv=1/std::hypot(dx,dy);dx*=inv;dy*=inv;
      float w=.0085f;
      // The analytic stroke supplies round caps after projection.
      for(int side=0;side<2;side++) {
        int v=eye*PerEye+ring*2+side;float width=(side?1.f:-1.f)*w;
        rest[v*2]=r*c-dy*width;rest[v*2+1]=r*s+dx*width;
      }
    }
    for(int i=0;i<MouthSteps;i++) {
      int a=PerEye*2+i*2,k=i*6;
      const uint16_t tri[]={uint16_t(a),uint16_t(a+2),uint16_t(a+1),uint16_t(a+1),uint16_t(a+2),uint16_t(a+3)};
      std::copy(tri,tri+6,indices+k);
    }
    return true;
  }
  template<class Deform>
  void pose(float intensity,float seconds,const float center[2][2],Deform deform) {
    amount=intensity;
    if(amount<=.001f)return;
    for(int eye=0;eye<2;eye++) {
      float a=(eye? -1.f:1.f)*seconds*4.1887902f,c=std::cos(a),s=std::sin(a);
      for(int i=0;i<PerEye;i++) {
        int v=eye*PerEye+i;float x=rest[v*2],y=rest[v*2+1];
        float source[3]={center[eye][0]+amount*(x*c-y*s),center[eye][1]+amount*(x*s+y*c),0};
        source[2]=DittoFace::surface(source[0],source[1])+.042f;
        deform(source,positions+v*3);
      }
    }
  }
  template<class Vertex>
  void poseMouth(float intensity,Vertex vertex) {
    mouth_amount=intensity;
    if(mouth_amount<=.001f)return;
    for(int i=0;i<MouthVertices;i++)vertex(i,positions+(PerEye*2+i)*3);
  }
  bool project(const float* model,const float* view,int width,int height) {
    if(amount<=.001f&&mouth_amount<=.001f)return false;
    float m[16];a3d::mat4Multiply(view,model,m);
    for(int part=0;part<3;part++) {
      min_y[part]=1e9f;max_y[part]=-1e9f;
      if((part<2?amount:mouth_amount)<=.001f)continue;
      int count=part<2?PerEye:MouthVertices;
      for(int i=0;i<count;i++) {
        int v=part*PerEye+i;const float* p=positions+v*3;
        float w=m[3]*p[0]+m[7]*p[1]+m[11]*p[2]+m[15];
        behind[v]=!std::isfinite(w)||w<=.05f;sw[v]=behind[v]?0:1/w;
        sx[v]=((m[0]*p[0]+m[4]*p[1]+m[8]*p[2]+m[12])*sw[v]+1)*width*.5f-.5f;
        sy[v]=((m[1]*p[0]+m[5]*p[1]+m[9]*p[2]+m[13])*sw[v]+1)*height*.5f-.5f;
        // Mouth vertices describe the fill, not the outside of the stroke.
        // Include its 1.05 px radius and antialias fringe across tile edges.
        float margin=part==2?1.65f:1.f;
        min_y[part]=std::min(min_y[part],sy[v]-margin);max_y[part]=std::max(max_y[part],sy[v]+margin);
      }
    }
    if(amount>.001f)for(int eye=0;eye<2;eye++)for(int i=0;i<=Steps;i++) {
      int v=eye*PerEye+i*2;
      auto& p=strokes[eye*(Steps+1)+i];
      p.x=(sx[v]+sx[v+1])*.5f;p.y=(sy[v]+sy[v+1])*.5f;
      p.iw=behind[v]||behind[v+1]?0:(sw[v]+sw[v+1])*.5f;
      p.radius=std::hypot(sx[v+1]-sx[v],sy[v+1]-sy[v])*.5f;
    }
    if(mouth_amount>.001f)for(int i=0;i<=MouthVertices;i++) {
      int local=i<=MouthSteps?i*2:(MouthVertices-1)-(i-MouthSteps-1)*2;
      if(i==MouthVertices)local=0;
      int v=PerEye*2+local;auto& p=strokes[2*(Steps+1)+i];
      p={sx[v],sy[v],behind[v]?0:sw[v]+.00015f,1.05f*mouth_amount};
    }
    // Bin the small paths once, before workers start. A row usually touches
    // only a handful of segments, so the raster need not reread the whole
    // spiral for every row. Coverage/depth math remains identical.
    binned=height<=MaxRows;
    if(binned)for(int part=0;part<3;part++) {
      if((part<2?amount:mouth_amount)<=.001f)continue;
      auto* masks=row_masks+part*MaxRows*2;
      std::fill(masks,masks+height*2,0u);
      const auto* path=strokes+(part<2?part*(Steps+1):2*(Steps+1));
      const int count=part<2?Steps+1:MouthVertices+1;
      for(int i=1;i<count;i++) {
        const auto& a=path[i-1];const auto& b=path[i];
        if(a.iw<=0 || b.iw<=0 || !std::isfinite(a.x+a.y+a.radius+b.x+b.y+b.radius))continue;
        float margin=std::max(a.radius,b.radius)+.6f;
        int lo=int(std::clamp(std::ceil(std::min(a.y,b.y)-margin),0.f,float(height)));
        int hi=int(std::clamp(std::floor(std::max(a.y,b.y)+margin),-1.f,float(height-1)));
        for(int y=lo;y<=hi;y++)masks[y*2+(i-1)/32]|=1u<<((i-1)%32);
      }
    }
    return true;
  }
  void draw(a3d::SoftBackend& renderer,int top,int rows) const {
    if(amount<=.001f&&mouth_amount<=.001f)return;
    a3d::Material ink;ink.color[0]=.07f;ink.color[1]=.025f;ink.color[2]=.09f;
    ink.ambient=1;ink.diffuse=ink.specular=0;
    renderer.setMaterial(ink);renderer.setShading(a3d::Shading::Unlit,a3d::TextureMode::None);
    float opacity=renderer.opacity();
    for(int part=0;part<3;part++) {
      float alpha=part<2?amount:mouth_amount;
      if(alpha<=.001f)continue;
      if(max_y[part]<top || min_y[part]>=top+rows)continue;
      renderer.setOpacity(alpha);
      if(part<2){renderer.drawProjectedStroke(strokes+part*(Steps+1),Steps+1,0x1062,amount,binned?row_masks+part*MaxRows*2:nullptr);continue;}
      if(part==2){ink.color[0]=.90f;ink.color[1]=.43f;ink.color[2]=.60f;renderer.setMaterial(ink);}
      a3d::ProjectedBatch b;b.nbTriangles=Triangles;
      b.indices=indices;
      b.screenX=sx;b.screenY=sy;b.screenW=sw;b.behind=behind;
      renderer.drawTrianglesProjected(b);
      renderer.drawProjectedStroke(strokes+2*(Steps+1),MouthVertices+1,0x1062,mouth_amount,binned?row_masks+2*MaxRows*2:nullptr);
    }
    renderer.setOpacity(opacity);
  }
};
