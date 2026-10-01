#include "motion_health.h"
#include <cassert>
#include <cstdio>
int main() {
  MotionHealth h;h.start(1000000);
  assert(!h.fresh(1000000));
  assert(h.observe(0xffffff,1010000));
  assert(h.observe(0,1020000)); // Valid counter wrap, not a stall.
  assert(!h.observe(0,1100000));
  assert(h.last_progress_us==1020000 && h.fresh(1100000));
  assert(!h.fresh(1180000) && !h.needsReset(1180000));
  assert(h.needsReset(1520000) && !h.needsReset(1600000));
  h.start(1540000);
  assert(h.observe(0,1550000) && h.fresh(1550000));
  assert(!h.needsReset(3000000) && h.needsReset(3520000));
  h.start(3540000);
  // A stationary sensor still has a progressing sample clock.
  for(int i=0;i<10000;i++) {
    int64_t now=3540000+int64_t(i)*17000;
    assert(h.observe(uint32_t(i),now));
    assert(h.fresh(now) && !h.needsReset(now));
  }
  puts("New-sample gating, duplicates, 24-bit wrap, freshness, stall recovery cooldown and stationary sampling passed.");
}
