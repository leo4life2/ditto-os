#pragma once
#include <algorithm>
#include <cmath>

namespace DittoEyes {
// Fixed closed-eyelid > contour, slightly wider and heavier than the ordinary dot.
// It replaces a closing dot; there is no dot-to-arrow growth morph.
inline constexpr float Chevron[21][2]={{0.029704453f,0.000000000f},{0.041417618f,0.000000000f},{0.037304023f,0.004381365f},{0.030286536f,0.006960219f},{0.016875522f,0.011838838f},{-0.012343166f,0.022467928f},{-0.032541804f,0.027344643f},{-0.037921853f,0.025756862f},{-0.041911846f,0.021537336f},{-0.020178568f,0.010974333f},{0.002706785f,0.002649164f},{0.009989174f,0.000000000f},{0.002706785f,-0.002649164f},{-0.020178568f,-0.010974333f},{-0.041911846f,-0.021537336f},{-0.037921853f,-0.025756862f},{-0.032541804f,-0.027344643f},{-0.012343166f,-0.022467928f},{0.016875522f,-0.011838838f},{0.030286536f,-0.006960219f},{0.037304023f,-0.004381365f}};

// One owner for eye geometry. Gaze translates the dots; it never reshapes
// them. Closure is selected once, not multiplied by other eyelid effects.
struct Pose {
  float open[2]={1,1},width=1,cross=0,gaze=0,gaze_y=0,spiral=0,chevron=0;
};
struct Controller {
  bool skip_blink=false;
  Pose resolvePose(float seconds,float sleep,float wink,float requested_blink,bool auto_blink,float gaze_x,float gaze_y,float dizzy=0) {
    Pose p;
    sleep=std::clamp(sleep,0.f,1.f);wink=std::clamp(wink,0.f,1.f);
    float phase=std::fmod(std::max(0.f,seconds),4.f);
    float triangle=std::max(0.f,1-std::abs(phase-2.65f)/.11f);
    float normal=.94f*triangle*triangle;
    if(sleep>.001f || dizzy>.001f || wink>.001f || requested_blink>0 || !auto_blink)skip_blink=true;
    else if(normal==0)skip_blink=false;
    if(sleep>.001f){p.open[0]=p.open[1]=1-.76f*sleep;p.width=1+.65f*sleep;}
    else if(dizzy>.001f)p.spiral=std::clamp(dizzy,0.f,1.f);
    else if(wink>.001f) {
      if(wink>=.80f)p.chevron=1;
      else p.open[0]=1-.70f*wink;
    }
    else if(requested_blink>0)p.open[0]=p.open[1]=1-.95f*std::clamp(requested_blink,0.f,1.f);
    else if(auto_blink&&!skip_blink)p.open[0]=p.open[1]=1-normal;
    p.gaze=gaze_x;p.gaze_y=gaze_y;return p;
  }
  // Compatibility for older host tools. Rejected cross/side morphs cannot
  // enter the renderer through this legacy call either.
  Pose resolve(float seconds,bool sleeping,float wink,float,float side,float gaze) {
    return resolvePose(seconds,sleeping?1.f:0.f,wink,0,true,.045f*std::clamp(gaze,-1.f,1.f)*std::clamp(side,0.f,1.f),0);
  }
};
}
