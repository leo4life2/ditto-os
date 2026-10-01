#include "viewer/a3d_viewer.h"
#include <cassert>
#include <cstdio>
#include <vector>
static void tile(void *ctx, const uint16_t *p, int x, int y, int w, int h) {
  auto &pixels = *static_cast<std::vector<uint16_t> *>(ctx);
  for (int row = 0; row < h; row++)
    memcpy(pixels.data() + (y + row) * 410 + x, p + row * w, w * 2);
}
int main(int argc, char **argv) {
  assert(argc == 2);
  std::vector<uint16_t> full(410 * 502), cropped(full.size());
  a3d::Viewer a, b;
  a3d::Display da, db;
  da.width = db.width = 410; da.height = db.height = 502;
  da.sendTile = db.sendTile = tile; da.user = &full; db.user = &cropped;
  a3d::ViewerConfig config;
  config.tileHeight = 16; config.culling = -1; config.texture = a3d::TextureMode::None;
  assert(a.begin(da, config));
  config.visibleRowBegin = 54; config.visibleRowEnd = 383;
  assert(b.begin(db, config));
  assert(a.openAssetFile(argv[1]) && b.openAssetFile(argv[1]));
  a.setLightDirection(-.3f, -.7f, -1); b.setLightDirection(-.3f, -.7f, -1);
  a.setOrbit(.12f, .05f, 3.55f); b.setOrbit(.12f, .05f, 3.55f);
  long compared = 0;
  for (int clip = 0; clip < 14; clip++) {
    a.selectClip(clip); b.selectClip(clip);
    for (int frame = 0; frame < 8; frame++) {
      a.frame(33.333f); b.frame(33.333f);
      for (int y = 54; y < 383; y++) for (int x = 0; x < 410; x++) {
        assert(full[y * 410 + x] == cropped[y * 410 + x]); compared++;
      }
    }
  }
  printf("%ld native-resolution pixels match the uncropped renderer across 112 frames / 14 clips.\n", compared);
}
