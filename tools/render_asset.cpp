// Render a review asset with the device's native software rasterizer.
#include "viewer/a3d_viewer.h"
#include "backends/threads/a3d_std_executor.h"
#include <cstdio>
#include <vector>
static constexpr int W=410,H=502;
static std::vector<uint16_t> pixels(W*H);
static void tile(void*,const uint16_t*p,int x,int y,int w,int h){for(int r=0;r<h;r++)memcpy(pixels.data()+(y+r)*W+x,p+r*w,w*2);}
int main(int argc,char**argv){
 if(argc<3){fprintf(stderr,"asset.a3d out.rgb565 [clip] [ms] [yaw] [distance] [frames] [pitch]\n");return 1;}
 a3d::Display d;d.width=W;d.height=H;d.sendTile=tile;
 a3d::StdThreadExecutor executor(2);a3d::Viewer v;v.setExecutor(executor);
 a3d::ViewerConfig c;c.tileHeight=20;c.culling=-1;c.texture=a3d::TextureMode::None;
 if(!v.begin(d,c)||!v.openAssetFile(argv[1])){fprintf(stderr,"%s\n",v.error());return 2;}
 std::vector<a3d::SoftBackend::ColorRamp> ramps(v.image().materialCount());
 for(int i=0;i<(int)ramps.size();i++){
  const auto&m=v.image().materials()[i];
  a3d::SoftBackend::makeSmoothColorRamp(ramps[i],m.color[0],m.color[1],m.color[2]);
 }
 for(int i=0;i<2;i++)v.backend(i).setColorRamps(ramps.data(),ramps.size());
 v.setLightDirection(-.35,-.65,-1);
 v.setOrbit(argc>5?atof(argv[5]):.13,argc>8?atof(argv[8]):.055,argc>6?atof(argv[6]):3.2);
 v.selectClip(argc>3?atoi(argv[3]):0);
 const float start=argc>4?atof(argv[4]):0;const int frames=argc>7?atoi(argv[7]):1;
 FILE*f=fopen(argv[2],"wb");if(!f)return 3;
 for(int i=0;i<frames;i++){v.seek(start+i*1000.f/30);v.frame(0);fwrite(pixels.data(),2,pixels.size(),f);}
 fclose(f);return 0;
}
