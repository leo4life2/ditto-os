#pragma once
#include "blob_motion.h"

namespace Activity {
constexpr int64_t idle_timeout_us=60000000;
// Separate from visual jiggle: only fresh physical input counts as activity.
// Consecutive evidence rejects single-sample knocks and ordinary resting noise.
struct Motion {
  BlobMotion::V anchor,measured;
  int64_t previous_us=0,evidence_us=0,active_until_us=0,reference_until_us=0;
  bool observe(const BlobMotion::Sample &sample) {
    if(!sample.valid || !BlobMotion::finite(sample.specific) ||
       !BlobMotion::finite(sample.omega))return false;
    int64_t now=sample.time_us,dt=now-previous_us;
    if(!previous_us || dt>150000 || dt<=0) {
      anchor=measured=sample.specific;previous_us=now;evidence_us=0;active_until_us=0;
      reference_until_us=now+120000;return false;
    }
    previous_us=now;
    measured=measured+(sample.specific-measured)*(1-std::exp(-float(dt)/60000.f));
    // Average the initial reference instead of anchoring it to one noisy
    // sample. The board is already awake during initial sensor startup.
    if(now<reference_until_us) {anchor=measured;return false;}
    // Use measured acceleration, not the visual filter's gravity estimate:
    // correcting gyro bias can move that estimate while the board is still.
    const bool evidence=std::abs(measured.length()-1.f)>.055f ||
                        sample.omega.length()>.12f ||
                        (measured-anchor).length()>.026f;
    if(evidence)evidence_us+=std::min<int64_t>(dt,35000);
    else evidence_us=0;
    if(evidence_us>=45000) {
      active_until_us=now+250000;
      // A new static pose becomes the rest reference. Slow rotation can
      // accumulate another ~1.5 degrees without needing a high angular speed.
      anchor=measured;
    }
    return now<active_until_us;
  }
};
inline bool shouldSleep(int64_t now,int64_t last_activity,bool touched,bool moving,
                        bool voice_active,bool fresh_motion) {
  return fresh_motion && !touched && !moving && !voice_active &&
         now-last_activity>=idle_timeout_us;
}
}
