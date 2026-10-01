#pragma once
#include <cstdint>

// AXP2101 read-only REG00/REG01; see Waveshare's X-power-AXP2101
// datasheet, PMU status1/status2. USB host enumeration is not charger status.
namespace Battery {
enum class Mode : uint8_t { Unknown, Absent, Discharging, Charging, Full, Paused };
struct Status {
  Mode mode=Mode::Unknown;
  bool external=false,present=false;
  uint8_t phase=7,direction=3;
};
inline Status decode(bool valid,uint8_t status0,uint8_t status1) {
  if(!valid)return {};
  Status s;
  s.external=status0&0x20;
  s.present=status0&0x08;
  s.phase=status1&7;
  s.direction=(status1>>5)&3;
  if(!s.present)s.mode=Mode::Absent;
  else if(!s.external)s.mode=Mode::Discharging;
  else if(s.direction==3 || s.phase>=6)s.mode=Mode::Unknown;
  else if(s.direction==2)s.mode=Mode::Paused;
  else if(s.phase==4 && s.direction==0)s.mode=Mode::Full;
  else if(s.phase<4)s.mode=Mode::Charging;
  else if(s.phase==5)s.mode=Mode::Paused;
  else s.mode=Mode::Unknown;
  return s;
}
inline const char* name(Mode mode) {
  switch(mode) {
    case Mode::Absent:return "absent";
    case Mode::Discharging:return "discharging";
    case Mode::Charging:return "charging";
    case Mode::Full:return "full";
    case Mode::Paused:return "paused";
    default:return "unknown";
  }
}
}
