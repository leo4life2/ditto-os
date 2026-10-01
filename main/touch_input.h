#pragma once
#include <cstdint>

struct TouchPoint { bool down = false; int x = 0, y = 0; };
// FT3168 registers 0x02..0x06, read in one transaction. Never turn a corrupt
// report into a corner touch; the caller releases a stale contact on timeout.
inline bool decode_touch(const uint8_t r[5], TouchPoint &p) {
  unsigned count = r[0] & 15, event = r[1] >> 6;
  if (count > 2) return false;
  if (!count || event == 1) { p.down = false; return true; }
  if (event == 3) return false;
  int x = ((r[1] & 15) << 8) | r[2], y = ((r[3] & 15) << 8) | r[4];
  if (x >= 410 || y >= 502) return false;
  p = {true, x, y};
  return true;
}

// Recover a stalled digitizer without resetting the display or OS. Isolated
// read failures keep their normal retry; persistent failure is rate limited.
struct TouchRecovery {
  unsigned consecutive=0;
  int64_t next_reset_us=0;
  void resetWindow(int64_t now) {consecutive=0;next_reset_us=now+2000000;}
  bool observe(bool ok,int64_t now,bool quiet_bus_deferral=false) {
    // FT3168 monitor mode may not answer I2C after another bus slave is used.
    // Without a physical contact or data-ready IRQ this is not a stalled
    // active report. Keep diagnostics, wait for IRQ, don't reset every few s.
    if(ok || quiet_bus_deferral){consecutive=0;return false;}
    if(consecutive<8)consecutive++;
    if(consecutive<8||now<next_reset_us)return false;
    resetWindow(now);return true;
  }
};
