#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// 16 kHz speech conditioning. No noise gate or slow upward AGC: quiet first
// syllables get the same gain as later speech. Codec analogue gain is 36 dB
// (12 dB above the old setting); this stage adds another 6 dB below its limiter.
class VoiceCapture {
 public:
  static constexpr float AnalogGainDb=36.f;
  static constexpr int BlockSamples=80; // 5 ms; AI transport still groups 20 ms.
  static constexpr int SettleSamples=3200; // Only after actual ADC power-up.
  void reset(){x1=x2=y1=y2=0;limiter=1;settling=SettleSamples;fade=0;}
  bool settlingNow() const{return settling>0;}
  void process(int16_t* samples,size_t count){
    float filtered[BlockSamples];
    // Butterworth high-pass, 85 Hz at 16 kHz, two poles. Handles DC and
    // handling/desk rumble without a speech detector clipping consonants.
    float peak=0;
    for(size_t i=0;i<count;i++){
      float x=samples[i]/32768.f;
      float y=.976673515f*x-1.953347030f*x1+.976673515f*x2+1.952802831f*y1-.953891229f*y2;
      x2=x1;x1=x;y2=y1;y1=y;
      if(settling>0){--settling;y=0;}
      else {fade=std::min(fade+1.f/160.f,1.f);y*=fade;}
      filtered[i]=y*2.f;peak=std::max(peak,std::abs(filtered[i]));
    }
    // A block of lookahead is already present in the 5 ms capture buffer.
    // Protect against taps/pops; restore gain gently after loud sounds.
    float target=peak>.82f?.82f/peak:1.f;
    float next=target<limiter?target:limiter+(target-limiter)*.02469f;
    float start=target<limiter?target:limiter;
    for(size_t i=0;i<count;i++){
      float g=start+(next-start)*float(i+1)/float(count);
      samples[i]=int16_t(std::clamp(filtered[i]*g,-.98f,.98f)*32767.f);
    }
    limiter=next;
  }
 private:
  float x1=0,x2=0,y1=0,y2=0,limiter=1,fade=0;
  int settling=SettleSamples;
};
