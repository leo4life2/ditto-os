// Regression: projected pixel-centre binning must preserve actual raster output.
// Run from the project root (writes only the named test executable):
// c++ -std=c++17 -O2 -fsanitize=address,undefined -fno-sanitize-recover=all \
//   -I components/a3d/src tests/pixel_center_binning_check.cpp -o /tmp/scout-pixel-bounds
// /tmp/scout-pixel-bounds
#include "a3d/a3d_binner.h"
#include "backends/soft/a3d_soft_backend.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

static constexpr int W=96,H=80,N=180,T=N/3;
static std::vector<uint16_t> render(bool exact,int tw,int th,int cull,bool blended,
                                  const std::vector<float>& positions,
                                  const std::vector<uint16_t>& indices) {
  a3d::TileBinner b; assert(b.configure(W,H,tw,th,T,N));
  b.setCulling(cull);b.setPixelCenterBounds(exact);
  float m[16];a3d::mat4Identity(m);m[11]=.7f;
  assert(b.bin(positions.data(),N,indices.data(),T,m));
  std::vector<float> normals(N*3);for(int i=0;i<N;i++)normals[3*i+2]=1;
  std::vector<uint16_t> screen(W*H),pixels(tw*th),z(tw*th),scratch(T*3);
  a3d::SoftBackend r;r.setViewportSize(W,H);r.setZBuffer(z.data());
  r.setCulling(cull);r.setShading(a3d::Shading::Gouraud,a3d::TextureMode::None);
  a3d::Material material;material.color[0]=.7f;material.color[1]=.4f;material.color[2]=.8f;
  r.setMaterial(material);r.setOpacity(blended?.5f:1.f);
  for(int tile=0;tile<b.tileCount();tile++) {
    int x,y,w,h;b.tileRect(tile,x,y,w,h);
    std::fill(pixels.begin(),pixels.end(),0);
    r.setTarget(pixels.data(),w,h,w);r.setOffset(x,y);r.clearZBuffer();
    int n=b.buildTileIndices(tile,indices.data(),scratch.data(),T);assert(n>=0);
    a3d::ProjectedBatch pb;pb.nbTriangles=n;pb.indices=scratch.data();
    pb.screenX=b.screenX();pb.screenY=b.screenY();pb.screenW=b.screenW();pb.behind=b.behind();
    pb.indNormals=scratch.data();pb.normals=normals.data();r.drawTrianglesProjected(pb);
    for(int row=0;row<h;row++)std::copy_n(pixels.data()+row*w,w,screen.data()+(y+row)*W+x);
  }
  return screen;
}
int main() {
  std::mt19937 rng(0xd177071e);std::uniform_real_distribution<float> coord(-3.f,3.f);
  std::vector<float> positions(N*3);std::vector<uint16_t> indices(N);
  for(int i=0;i<N;i++)indices[i]=i;
  int cases=0;
  for(int scene=0;scene<160;scene++) {
    for(int i=0;i<N;i++) {
      positions[3*i]=coord(rng);positions[3*i+1]=coord(rng);
      positions[3*i+2]=scene%3==0?coord(rng):0;
      // Keep visible test vertices within the fixed-depth renderer's range.
      // Behind-eye vertices still exercise the conservative fallback. A first
      // unconstrained fixture hit the existing fixed-depth overflow near w=0.
      if(1.f+.7f*positions[3*i+2]<.4f)positions[3*i+2]=-3.f;
    }
    // Tiny triangles and exact/neighbouring pixel and tile boundaries. The
    // projected floats from the same binner feed both actual rasterizers.
    if(scene%4==0)for(int i=0;i<N;i++) {
      float px=float((i/3*13+scene)%110)-5.f+.5f;
      float py=float((i/3*7+scene)%94)-6.f+.5f;
      if(i%3==1)px=std::nextafter(px,INFINITY);
      if(i%3==2){px+=.8f;py+=1.1f;}
      positions[3*i]=(px+.5f)*2/W-1;
      positions[3*i+1]=(py+.5f)*2/H-1;positions[3*i+2]=0;
    }
    for(int cull:{-1,0,1})for(bool blended:{false,true}) {
      int tw=scene%2?W:13,th=scene%3==0?1:scene%3==1?18:27;
      auto conservative=render(false,tw,th,cull,blended,positions,indices);
      auto exact=render(true,tw,th,cull,blended,positions,indices);
      if(conservative!=exact){fprintf(stderr,"Mismatch scene %d cull %d blend %d\n",scene,cull,blended);return 1;}
      cases++;
    }
  }
  printf("{\"passed\":true,\"scene_variants\":%d,\"pixels_compared\":%d}\n",cases,cases*W*H);
}
