#pragma once
#include "a3d/a3d_binner.h"
#include <algorithm>
#include <cmath>

namespace Blob {
struct Hit {bool hit=false;float x=0,y=0,z=0,nx=0,ny=0,nz=1;int triangle=-1;};
// Reuse the exact last-presented vertex projection. Perspective-correct
// barycentrics recover the touched material point, including a deformed pose.
inline Hit pick(const a3d::TileBinner &b,const uint16_t *indices,int triangles,
                const float *rest,const float *normals,float px,float py) {
  Hit best;float nearest=0;
  const float *sx=b.screenX(),*sy=b.screenY(),*sw=b.screenW();
  const uint8_t *behind=b.behind();
  for(int t=0;t<triangles;t++) {
    int a=indices[3*t],c=indices[3*t+1],d=indices[3*t+2];
    if(behind[a]||behind[c]||behind[d])continue;
    if(px<std::min({sx[a],sx[c],sx[d]})||px>std::max({sx[a],sx[c],sx[d]})||
       py<std::min({sy[a],sy[c],sy[d]})||py>std::max({sy[a],sy[c],sy[d]}))continue;
    float det=(sy[c]-sy[d])*(sx[a]-sx[d])+(sx[d]-sx[c])*(sy[a]-sy[d]);
    if(std::abs(det)<1e-6f)continue;
    float wa=((sy[c]-sy[d])*(px-sx[d])+(sx[d]-sx[c])*(py-sy[d]))/det;
    float wb=((sy[d]-sy[a])*(px-sx[d])+(sx[a]-sx[d])*(py-sy[d]))/det;
    float wc=1-wa-wb;
    if(wa<0||wb<0||wc<0)continue;
    wa*=sw[a];wb*=sw[c];wc*=sw[d];float depth=wa+wb+wc;
    if(depth<=nearest)continue;
    nearest=depth;wa/=depth;wb/=depth;wc/=depth;
    best={true,rest[a*3]*wa+rest[c*3]*wb+rest[d*3]*wc,
      rest[a*3+1]*wa+rest[c*3+1]*wb+rest[d*3+1]*wc,
      rest[a*3+2]*wa+rest[c*3+2]*wb+rest[d*3+2]*wc,
      normals[a*3]*wa+normals[c*3]*wb+normals[d*3]*wc,
      normals[a*3+1]*wa+normals[c*3+1]*wb+normals[d*3+1]*wc,
      normals[a*3+2]*wa+normals[c*3+2]*wb+normals[d*3+2]*wc,t};
  }
  return best;
}
}
