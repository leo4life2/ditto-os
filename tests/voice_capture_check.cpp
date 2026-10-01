#include "voice_capture.h"
#include <cassert>
#include <cstdio>
#include <vector>
static std::vector<int16_t> tone(VoiceCapture& dsp,float hz,float level,int blocks=200,float dc=0){
 std::vector<int16_t> out;
 for(int b=0;b<blocks;b++){
  int16_t a[80];for(int i=0;i<80;i++)a[i]=int16_t((dc+level*sin(6.283185307179586*hz*(b*80+i)/16000))*32767);
  dsp.process(a,80);out.insert(out.end(),a,a+80);
 }
 return out;
}
static float rms(const std::vector<int16_t>&a,size_t start=8000){double s=0;for(size_t i=start;i<a.size();i++)s+=double(a[i])*a[i];return sqrt(s/(a.size()-start))/32768;}
int main(){
 VoiceCapture dsp;
 auto voice=tone(dsp,1000,.02f);float r=rms(voice);assert(r>.027f&&r<.029f);
 // After wake, a full-scale ADC transient must not reach USB. Steady operation
 // does not reset this settling period when an app opens/closes its input.
 dsp.reset();auto pop=tone(dsp,100,.95f,40);for(auto v:pop)assert(v==0);
 auto loud=tone(dsp,1000,.95f);for(auto v:loud)assert(abs(int(v))<=26870);
 auto recovered=tone(dsp,1000,.02f,400);assert(rms(recovered,24000)>.027f);
 dsp.reset();auto dc=tone(dsp,0,0,200,.7f);assert(rms(dc)<.0001f);
 dsp.reset();auto rumble=tone(dsp,20,.1f);assert(rms(rumble)<.009f);
 dsp.reset();auto speech=tone(dsp,300,.1f);assert(rms(speech)>.137f);
 // Silence stays silent; resetting one long-lived processor restores gain.
 dsp.reset();auto quiet=tone(dsp,1000,0);for(auto v:quiet)assert(v==0);
 dsp.reset();auto first=tone(dsp,1000,.02f,44);int peak=0;for(size_t i=3360;i<first.size();i++)peak=std::max(peak,abs(int(first[i])));assert(peak>1250);
 puts("Speech gain, rumble/DC rejection, wake transient, first syllable, limiter and recovery passed");
}
