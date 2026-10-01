#include "viewer/a3d_viewer.h"
#include "backends/threads/a3d_std_executor.h"
#include <cstdio>
#include <vector>
static constexpr int W = 410, H = 502;
static std::vector<uint16_t> pixels(W * H);
static void tile(void *, const uint16_t *p, int x, int y, int w, int h) {
  for (int r = 0; r < h; r++)
    memcpy(pixels.data() + (y + r) * W + x, p + r * w, w * 2);
}
int main(int argc, char **argv) {
  if (argc < 3)
    return 1;
  a3d::Display d;
  d.width = W;
  d.height = H;
  d.sendTile = tile;
  a3d::StdThreadExecutor executor(argc > 4 ? atoi(argv[4]) : 1);
  a3d::Viewer v;
  v.setExecutor(executor);
  a3d::ViewerConfig c;
  c.tileHeight = argc > 6 ? atoi(argv[6]) : 20;
  c.culling = argc > 3 ? atoi(argv[3]) : 0;
  c.texture = a3d::TextureMode::None;
  if (!v.begin(d, c) || !v.openAssetFile(argv[1]))
    return 2;
  std::vector<a3d::SoftBackend::ColorRamp> ramps(v.image().materialCount());
  for (int i = 0; i < (int)ramps.size(); i++) {
    const auto& m = v.image().materials()[i];
    #ifdef A3D_SMOOTH_COLOR
    a3d::SoftBackend::makeSmoothColorRamp(ramps[i], m.color[0], m.color[1], m.color[2]);
    #else
    a3d::SoftBackend::makeColorRamp(ramps[i], m.color[0], m.color[1], m.color[2]);
    #endif
  }
  #ifndef A3D_DISABLE_COLOR_RAMP
  for (int i = 0; i < executor.workerCount(); i++) v.backend(i).setColorRamps(ramps.data(), ramps.size());
  #endif
  v.setLightDirection(-.3, -.7, -1);
  v.setOrbit(.12, .05, 3.2);
  uint8_t fade[H];
  uint16_t smoothFade[H];
  for (int y = 0; y < H; y++) {
    int gy = y + 42, light = 32;
    if (gy < 166) light = std::max(0, std::min(32, (gy - 96) * 32 / 70));
    if (gy > 318) light = std::max(0, std::min(32, 32 - (gy - 318) * 32 / 106));
    fade[y] = light;
    int fineLight = 256;
    if (gy < 166) fineLight = std::max(0, std::min(256, (gy - 96) * 256 / 70));
    if (gy > 318) fineLight = std::max(0, std::min(256, 256 - (gy - 318) * 256 / 106));
    smoothFade[y] = fineLight;
  }
  const bool shaderFade = argc > 5 && !strcmp(argv[5], "shader");
  const bool postFade = argc > 5 && !strcmp(argv[5], "post");
  #ifdef A3D_SMOOTH_COLOR
  if (shaderFade)
    for (int i = 0; i < executor.workerCount(); i++) v.backend(i).setSmoothRowFade(smoothFade);
  #else
  if (shaderFade)
    for (int i = 0; i < executor.workerCount(); i++) v.backend(i).setRowFade(fade);
  #endif
  FILE *out = fopen(argv[2], "wb");
  if (!out)
    return 3;
  for (int clip = 0; clip < int(v.clipCount()); clip++) {
    v.selectClip(clip);
    for (int frame = 0; frame < 8; frame++) {
      v.seek(v.clipDurationMs() * frame / 8.f);
      v.frame(0.f);
      if (postFade)
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
          auto& pixel = pixels[y*W+x];
          uint32_t packed = (pixel | (uint32_t(pixel) << 16)) & 0x07e0f81f;
          packed = ((packed * fade[y]) >> 5) & 0x07e0f81f;
          pixel = uint16_t(packed | (packed >> 16));
        }
      fwrite(pixels.data(), 2, pixels.size(), out);
    }
  }
  fclose(out);
  return 0;
}
