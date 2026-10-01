#pragma once
#include "a3d/a3d_binner.h"
#include "backends/soft/a3d_soft_backend.h"
#include "scene_damage.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// A small green light follows the upper body. A native-resolution soft stamp
// is baked once, then translated behind the live geometry; no per-frame blur
// or full-screen background is needed. Movement participates in scene damage.
struct DittoListeningGlow {
  static constexpr int Width=410,Height=502,Levels=6,StampWidth=400,StampHeight=208;
  uint16_t* background=nullptr;
  const uint16_t* cached_indices=nullptr;
  int vertices=0,origin_x=0,origin_y=0,level=0;
  SceneDamageBounds bounds,dirty_bounds;
  bool visible=false,fast_background=true,use_cached_background=true;
  uint32_t mask_us=0,blur_us=0,palette_us=0;
  static uint16_t sample(int x,int y,int l) {
    constexpr float dither[4]={.125f,.625f,.875f,.375f};
    const float dx=(x+.5f-StampWidth*.5f)/194.f,dy=(y+.5f-StampHeight*.5f)/100.f;
    const float t=std::max(0.f,1-dx*dx-dy*dy);
    const float a=t*t*(l+1)/Levels*.55f,d=dither[(x&1)+2*(y&1)];
    return uint16_t((int(83.f/255*31*a+d)<<11)|(int(235.f/255*63*a+d)<<5)|int(120.f/255*31*a+d));
  }
  bool begin(void*(*allocate)(size_t),void*(*large)(size_t)=nullptr) {
    background=static_cast<uint16_t*>((large?large:allocate)(size_t(StampWidth)*StampHeight*Levels*sizeof(uint16_t)));
    if(!background)return false;
    for(int l=0;l<Levels;l++)for(int y=0;y<StampHeight;y++)for(int x=0;x<StampWidth;x++)
      background[(size_t(l)*StampHeight+y)*StampWidth+x]=sample(x,y,l);
    return true;
  }
  bool prepare(const a3d::TileBinner& b,const uint16_t* indices,int triangles,
               int h,float intensity,float,uint64_t(*clock)(void*)=nullptr) {
    const auto old_bounds=bounds;const bool was_visible=visible;
    const int old_x=origin_x,old_y=origin_y;
    mask_us=blur_us=palette_us=0;visible=false;bounds={};dirty_bounds={};
    level=std::clamp(int(intensity*Levels+.5f),0,Levels);
    if(!background||h!=Height||!level)return false;
    const auto stamp=clock?clock(nullptr):0;
    if(cached_indices!=indices) {
      vertices=0;for(int i=0;i<triangles*3;i++)vertices=std::max(vertices,int(indices[i])+1);
      cached_indices=indices;
    }
    float left=1e9f,right=-1e9f,top=1e9f;
    const auto* x=b.screenX();const auto* y=b.screenY();const auto* behind=b.behind();
    for(int i=0;i<vertices;i++)if(!behind[i]&&std::isfinite(x[i]+y[i])) {
      left=std::min(left,x[i]);right=std::max(right,x[i]);top=std::min(top,y[i]);
    }
    if(left>right)return false;
    // Two-pixel alignment preserves the dither phase and fast paired copies.
    origin_x=int(std::lround((left+right)*.5f-StampWidth*.5f))&~1;
    origin_y=int(std::lround(top+38-StampHeight*.5f))&~1;
    bounds.point(origin_x,origin_y,0);bounds.point(origin_x+StampWidth-1,origin_y+StampHeight-1,0);
    // An unchanged stamp needs no presentation outside the moving body.
    // When it moves, erase its old footprint as well as drawing the new one.
    // Intensity transitions already select the full-render fallback.
    if(!was_visible||old_x!=origin_x||old_y!=origin_y) {
      dirty_bounds=bounds;dirty_bounds.unite(old_bounds);
    }
    mask_us=clock?clock(nullptr)-stamp:0;visible=true;return true;
  }
  bool paintBackground(uint16_t* tile,int top,int rows,int width) const {
    if(!visible||!use_cached_background||width!=Width)return false;
    memset(tile,0,size_t(rows)*Width*sizeof(uint16_t));
    const int left=std::max(0,origin_x),right=std::min(Width,origin_x+StampWidth);
    if(left>=right)return true;
    using Pair=uint32_t __attribute__((__may_alias__));
    for(int y=std::max(top,origin_y);y<std::min(top+rows,origin_y+StampHeight);y++) {
      auto* dst=reinterpret_cast<Pair*>(tile+(y-top)*Width+left);
      const auto* src=reinterpret_cast<const Pair*>(background+
        (size_t(level-1)*StampHeight+y-origin_y)*StampWidth+left-origin_x);
      for(int i=0;i<(right-left)/2;i++)dst[i]=src[i];
    }
    return true;
  }
  // Slow analytic reference for exact native pixel comparisons.
  void draw(a3d::SoftBackend& renderer,int top,int rows) const {
    if(!visible||use_cached_background)return;
    const int left=std::max(0,origin_x),right=std::min(Width,origin_x+StampWidth);
    if(left>=right)return;
    for(int y=std::max({0,top,origin_y});y<std::min({Height,top+rows,origin_y+StampHeight});y++)
      renderer.drawProjectedPatch(left,y,right-left,1,.01f,[&](uint16_t bg,int x,int yy){
        return bg?bg:sample(x-origin_x,yy-origin_y,level-1);
      });
  }
};
