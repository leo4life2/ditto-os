#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

// A single producer (ADC) and consumer (USB). USB emits exactly 16 samples per
// millisecond; a gentle fractional resampler absorbs the independent ADC clock.
// Three ADC blocks prime playback, leaving two blocks of scheduling headroom.
class UsbPcm {
 public:
  static constexpr unsigned Capacity=2048, Packet=16, Prime=960;
  bool push(const int16_t* p,size_t n) {
    uint32_t w=write_.load(std::memory_order_relaxed),r=read_.load(std::memory_order_acquire);
    if(n>Capacity-(w-r))return false;
    for(size_t i=0;i<n;i++)data_[(w+i)&(Capacity-1)]=p[i];
    write_.store(w+n,std::memory_order_release);return true;
  }
  void reset() {read_.store(write_.load(std::memory_order_acquire),std::memory_order_release);primed_=false;fraction_=0;level_=800;}
  bool pop(int16_t* p) {
    uint32_t r=read_.load(std::memory_order_relaxed),w=write_.load(std::memory_order_acquire);
    if(!primed_) {
      if(w-r<Prime){std::fill_n(p,Packet,0);return true;}
      primed_=true;fraction_=0;level_=800;
    }
    level_+=.001f*(float(w-r)-level_);
    const float step=std::clamp(1.f+(level_-800.f)*.00005f,.99f,1.01f);
    for(unsigned i=0;i<Packet;i++) {
      if(w-r<2) {std::fill(p+i,p+Packet,0);read_.store(r,std::memory_order_release);primed_=false;return false;}
      float a=data_[r&(Capacity-1)],b=data_[(r+1)&(Capacity-1)];
      p[i]=int16_t(std::lround(a+(b-a)*fraction_));
      fraction_+=step;unsigned advance=unsigned(fraction_);fraction_-=advance;r+=advance;
    }
    read_.store(r,std::memory_order_release);return true;
  }
 private:
  int16_t data_[Capacity]{};
  std::atomic<uint32_t> read_{0},write_{0};
  bool primed_=false;
  float fraction_=0,level_=800;
};
