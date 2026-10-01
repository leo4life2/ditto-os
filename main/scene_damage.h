#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
// Conservative inclusive pixel bounds; presentation rounds them outward to
// even panel coordinates. An empty rectangle changes no pixels.
struct SceneDamageBounds {
  int left=410,right=-1,top=502,bottom=-1;
  bool valid=true;
  void point(float x,float y,float margin=2) {
    if(!std::isfinite(x+y+margin)){valid=false;return;}
    left=std::min(left,int(std::floor(std::clamp(x-margin,0.f,410.f))));
    right=std::max(right,int(std::ceil(std::clamp(x+margin,-1.f,409.f))));
    top=std::min(top,int(std::floor(std::clamp(y-margin,0.f,502.f))));
    bottom=std::max(bottom,int(std::ceil(std::clamp(y+margin,-1.f,501.f))));
  }
  void unite(const SceneDamageBounds& b) {
    left=std::min(left,b.left);right=std::max(right,b.right);
    top=std::min(top,b.top);bottom=std::max(bottom,b.bottom);valid&=b.valid;
  }
};
