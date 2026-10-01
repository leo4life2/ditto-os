#include "display_window.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <random>
#include <vector>

int main() {
  constexpr int W = 410, H = 502, DMA = 240 * 64;
  std::mt19937 random(5300);
  std::vector<unsigned short> shadow(W * H), panel(W * H);
  for (int i = 0; i < W * H; i++)
    shadow[i] = panel[i] = i * 137;
  int odd_requests = 0, strips = 0;
  for (int attempt = 0; attempt < 10000; attempt++) {
    int x = random() % W, y = random() % H;
    int w = 1 + random() % (W - x), h = 1 + random() % (H - y);
    if (attempt == 0) {
      x = 0;
      y = 0;
      w = W;
      h = H;
    }
    if (attempt == 1) {
      x = W - 1;
      y = H - 1;
      w = h = 1;
    }
    odd_requests += (x | y | w | h) & 1;
    const auto a = display_window(x, y, w, h);
    assert(a.x <= x && a.y <= y && a.x + a.width >= x + w &&
           a.y + a.height >= y + h);
    assert(a.x >= 0 && a.y >= 0 && a.x + a.width <= W && a.y + a.height <= H);
    auto expected = panel;
    for (int row = y; row < y + h; row++)
      for (int col = x; col < x + w; col++)
        shadow[row * W + col] = expected[row * W + col] = random();
    const int step = display_strip_rows(DMA, a.width);
    assert(step >= 2);
    for (int row = 0; row < a.height; row += step) {
      int n = std::min(step, a.height - row);
      assert(((a.x | (a.y + row) | (a.x + a.width) | (a.y + row + n)) & 1) ==
             0);
      assert(a.width * n <= DMA);
      for (int j = 0; j < n; j++)
        std::copy_n(shadow.data() + (a.y + row + j) * W + a.x, a.width,
                    panel.data() + (a.y + row + j) * W + a.x);
      strips++;
    }
    assert(panel == expected);
  }
  printf("10000 windows passed; %d unaligned inputs, %d aligned strips; "
         "all requested pixels and surrounding borders correct.\n",
         odd_requests, strips);
}
