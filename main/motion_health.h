#pragma once
#include <cstdint>

// A successful I2C read is not proof that the sensor produced a new sample.
// QMI8658's 24-bit counter advances at the sensor ODR, including at rest.
struct MotionHealth {
  uint32_t timestamp=0;
  int64_t last_progress_us=0,next_reset_us=0;
  bool seen=false;
  void start(int64_t now) {seen=false;last_progress_us=now;}
  bool observe(uint32_t stamp,int64_t now) {
    stamp&=0xffffff;
    if(seen && stamp==timestamp)return false;
    timestamp=stamp;seen=true;last_progress_us=now;return true;
  }
  bool fresh(int64_t now) const {return seen && now-last_progress_us<=150000;}
  bool needsReset(int64_t now) {
    if(now-last_progress_us<500000 || now<next_reset_us)return false;
    next_reset_us=now+2000000;return true;
  }
};
