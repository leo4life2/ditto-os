#include "ditto_listening_glow.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main() {
  DittoListeningGlow glow;
  assert(glow.begin(malloc));
  glow.visible=true;
  constexpr int xs[]={-400,-198,-2,0,128,400,410};
  constexpr int ys[]={-208,-102,-2,0,398,502};
  unsigned cases=0;
  std::vector<uint16_t> tile(410*18);
  for(int level=1;level<=glow.Levels;level++)for(int x:xs)for(int y:ys) {
    glow.level=level;glow.origin_x=x;glow.origin_y=y;
    for(int top=0;top<502;top+=18) {
      const int rows=std::min(18,502-top);
      std::fill(tile.begin(),tile.end(),0xffff);
      assert(glow.paintBackground(tile.data(),top,rows,410));
      for(int row=0;row<rows;row++)for(int col=0;col<410;col++) {
        const int sx=col-x,sy=top+row-y;
        const auto expected=sx>=0&&sx<glow.StampWidth&&sy>=0&&sy<glow.StampHeight?
          glow.sample(sx,sy,level-1):0;
        assert(tile[row*410+col]==expected);
      }
      cases++;
    }
  }
  free(glow.background);
  printf("%u clipped tile/intensity cases match the analytic green glow exactly.\n",cases);
}
