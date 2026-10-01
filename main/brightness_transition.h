#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Sleep is a soft landing; wake is a quick response. Retarget from the
// brightness actually reached, including a pickup halfway through dimming.
struct BrightnessTransition {
  static constexpr int64_t sleep_us=1600000,wake_us=180000;
  bool initialized=false,asleep=false;
  float current=55,from=55,target=55;
  int64_t started=0,duration=0;
  float value(int64_t now) const {
    if(!duration)return target;
    float t=std::clamp(float(now-started)/duration,0.f,1.f);
    float f=asleep?t*t*t*(t*(t*6-15)+10):1-(1-t)*(1-t)*(1-t);
    return from+(target-from)*f;
  }
  bool active(int64_t now) const {return duration && now-started<duration;}
  int update(bool sleeping,int setting,int64_t now) {
    float next=sleeping?10:std::clamp(setting,1,100);
    if(!initialized) {
      initialized=true;asleep=sleeping;current=from=target=next;
    } else {
      current=value(now);
      if(sleeping!=asleep) {
        from=current;target=next;started=now;
        duration=sleeping?sleep_us:wake_us;asleep=sleeping;
      } else if(next!=target) {
        // Ordinary brightness adjustments are immediate. A setting change
        // during wake keeps the short wake transition continuous.
        if(active(now)) {from=current;target=next;started=now;duration=wake_us;}
        else {current=from=target=next;duration=0;}
      }
    }
    return std::clamp(int(std::lround(current)),1,100);
  }
};
