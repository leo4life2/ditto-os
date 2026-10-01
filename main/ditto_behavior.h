#pragma once
#include "blob_physics.h"
#include <algorithm>
#include <cmath>

// Event time, never demo playback time. Physics is stepped independently and
// immediately; this controller only owns attention, mouth and ambient poses.
namespace DittoBehavior {
// Every horizontal oval gets the same deliberate pause once fully formed.
// Gesture acquisition, face transitions and physics keep their quick response.
inline constexpr float oval_hold=2.3f;
inline constexpr float tap_open_delay=.020f+.180f+oval_hold;
inline float ease(float a,float b,float t) {
  float x=std::clamp((t-a)/(b-a),0.f,1.f);return x*x*(3-2*x);
}
inline float hold(float a,float b,float c,float d,float t) {return ease(a,b,t)*(1-ease(c,d,t));}
struct Mouth {float oval=0,melon=0;};
inline Mouth mix(Mouth a,Mouth b,float f){return {a.oval+(b.oval-a.oval)*f,a.melon+(b.melon-a.melon)*f};}
enum class Mode {Idle,Tap,Pull,Flick,Hold,Tilt,Swing,Shake,Sleep};
inline const char* name(Mode m) {
  static const char* names[]={"idle","tap","pull","flick","hold","tilt","swing","shake","sleep"};
  return names[int(m)];
}
struct Pose {
  Mouth mouth;
  float dizzy=0;
  float gaze_x=0,gaze_y=0,blink=0,sleep=0,breath=0,pitch=0,roll=0;
  bool auto_blink=true,quiet=true;
};
struct Controller {
  Pose pose;
  Mode mode=Mode::Idle;
  float now=0,started=0,last_contact=-20,released=-20,last_move=0;
  float mouth_at=0,mouth_duration=.18f,melon_at=-20,melon_duration=.23f,smile_at=-20,smile_duration=.5f;
  Mouth mouth_from,reaction_mouth;
  float blink_at=-20,end_at=0,motion_last=-20,tilt_last=-20,quiet_since=0;
  float next_glance=2,glance_at=-20,sleep_time=0,sleep_from=0;
  float contact_x=205,contact_y=250,last_x=205,last_y=250,peak_speed=0;
  float sleep_switch_at=0,voice_mix=0;
  float shake_charge=0,dizzy_remaining=0;
  uint32_t seen_press=0,group=0,seed=0x915e;
  bool down=false,was_sleep=false,was_speaking=false,tilt_initialized=false;
  BlobMotion::V tilt_reference;
  static constexpr float forever=1e8f;
  void oval(float delay,float duration,float open_delay,float open_duration=.23f,float open_hold=.60f,float recover=.50f) {
    mouth_from=pose.mouth;mouth_at=now+delay;mouth_duration=duration;
    melon_at=open_delay>=forever?forever:now+std::max(open_delay,delay+duration+oval_hold);
    melon_duration=open_duration;
    smile_at=melon_at+open_duration+open_hold;smile_duration=recover;
    end_at=smile_at+recover;blink_at=end_at+.225f;
  }
  void release(float delay=.12f,float duration=.26f,float hold_time=.46f,float recovery=.49f) {
    // The held pose may be interrupted halfway through opening. Start exactly
    // there rather than assuming a completed oval or restarting a tap clip.
    mouth_from=pose.mouth;mouth_at=now;mouth_duration=std::max(.001f,delay);
    // Pull, hold, shake and swing releases share the same oval pause. A new
    // gesture can still replace it immediately; this never queues playback.
    melon_at=now+mouth_duration+oval_hold;melon_duration=duration;smile_at=melon_at+duration+hold_time;
    smile_duration=recovery;end_at=smile_at+recovery;blink_at=end_at+.225f;
  }
  Mouth mouthAt() const {
    if(now<melon_at)return mix(mouth_from,{1,0},ease(mouth_at,mouth_at+mouth_duration,now));
    if(now<smile_at)return mix({1,0},{0,1},ease(melon_at,melon_at+melon_duration,now));
    return mix({0,1},{0,0},ease(smile_at,smile_at+smile_duration,now));
  }
  void cancelReaction() {group=0;down=false;end_at=now;blink_at=-20;mode=Mode::Idle;quiet_since=now;}
  void step(float dt,const Blob::Touch& touch,bool hit,const BlobMotion::Sample& motion,
            const BlobMotion::Liquid& liquid,bool sleeping,bool speaking,float speech) {
    dt=std::clamp(dt,0.f,.1f);now+=dt;
    // The existing sensor classifier already rejects pickups and slow swings.
    // Require sustained qualified shaking as well before becoming dizzy.
    if(sleeping || speaking || touch.press!=seen_press) {
      shake_charge=0;dizzy_remaining=0;pose.dizzy=0;
    } else {
      bool vigorous=motion.valid && liquid.recovery.shaking &&
                    liquid.recovery.time-liquid.recovery.last_peak<.32f;
      shake_charge=std::clamp(shake_charge+(vigorous?dt:-2*dt),0.f,1.6f);
      if(shake_charge>=1.6f || (vigorous && dizzy_remaining>0))dizzy_remaining=4.5f;
      else dizzy_remaining=std::max(0.f,dizzy_remaining-dt);
      pose.dizzy+=((dizzy_remaining>0?1.f:0.f)-pose.dizzy)*(1-std::exp(-dt*8));
      if(pose.dizzy<.001f)pose.dizzy=0;
      else if(pose.dizzy>.999f)pose.dizzy=1;
    }
    if(sleeping!=was_sleep) {
      sleep_from=pose.sleep;sleep_switch_at=now;sleep_time=0;cancelReaction();
      reaction_mouth={};voice_mix=0;was_speaking=false;
      was_sleep=sleeping;mode=sleeping?Mode::Sleep:Mode::Idle;
      if(!sleeping){pose.mouth={};next_glance=now+5;}
    }
    pose.sleep=sleep_from+((sleeping?1.f:0.f)-sleep_from)*ease(0,sleeping?1.f:.18f,now-sleep_switch_at);
    if(sleeping)sleep_time+=dt;
    pose.breath=std::sin(6.2831853f*now/5.5f);
    if(sleeping) {
      seen_press=touch.press;pose.gaze_x*=std::exp(-dt*10);pose.gaze_y=-.018f*pose.sleep;
      pose.pitch=0;pose.roll=-.032f*pose.sleep;pose.blink=0;pose.auto_blink=false;pose.quiet=true;
      pose.mouth=mix(pose.mouth,{},1-std::exp(-dt*8));return;
    }
    pose.mouth=reaction_mouth;
    const bool new_contact=touch.press!=seen_press;
    if(new_contact) {
      seen_press=touch.press;
      if(hit) {
        const bool repeat=mode==Mode::Tap && now-last_contact<1.2f;
        group=repeat?std::min<uint32_t>(3,group+1):1;
        if(!repeat) {
          started=now;mode=Mode::Tap;
          oval(.020f,.180f,tap_open_delay);
        } else {
          // Extend the current reaction; never accumulate a queue of emotes.
          if(now>melon_at) {mouth_from=pose.mouth;mouth_at=now;mouth_duration=.18f;}
          melon_at=std::max(melon_at,now+tap_open_delay);smile_at=melon_at+.830f;
          end_at=smile_at+.5f;blink_at=group>=3?now+.73f:end_at+.225f;
        }
        contact_x=touch.press_x;contact_y=touch.press_y;last_x=touch.x;last_y=touch.y;
        last_contact=last_move=now;released=-20;peak_speed=0;down=true;quiet_since=now;
      }
    }
    if(down && touch.down) {
      const float dx=float(touch.x)-last_x,dy=float(touch.y)-last_y;
      const float speed=std::hypot(dx,dy)/std::max(dt,.004f);
      peak_speed=std::max(peak_speed,speed);
      if(std::hypot(dx,dy)>1.5f)last_move=now;
      last_x=touch.x;last_y=touch.y;
      float travel=std::hypot(float(touch.x)-contact_x,float(touch.y)-contact_y),age=now-last_contact;
      if(mode==Mode::Tap && travel>=13) {
        mode=peak_speed>330 && age<.45f?Mode::Flick:Mode::Pull;
        group=1;blink_at=-20;
        if(mode==Mode::Flick)oval(0,.16f,std::max(.23f,.895f-age));
        else oval(std::max(0.f,.45f-age),.35f,forever);
      }
      if((mode==Mode::Tap && age>.48f) || (mode==Mode::Pull && age>.85f && now-last_move>.30f)) {
        mode=Mode::Hold;blink_at=-20;oval(0,.22f,forever);
      }
      // A flick that becomes a sustained pull is a pull, not a queued flick.
      if(mode==Mode::Flick && age>.55f){mode=Mode::Pull;oval(0,.15f,forever);}
    }
    if(down && !touch.down) {
      down=false;released=now;
      if(mode==Mode::Pull)release();
      else if(mode==Mode::Hold)release(.10f,.26f,.44f,.50f);
    }
    float tx=0,ty=0,pitch=0,roll=0,blink=0;
    const bool contactReaction=mode==Mode::Tap||mode==Mode::Pull||mode==Mode::Flick||mode==Mode::Hold;
    if(contactReaction) {
      float age=now-started,contact_age=now-last_contact;
      if(mode==Mode::Tap) {
        float attention=hold(.30f,.63f,1.06f+(group-1)*.60f,1.56f+(group-1)*.60f,age);
        tx=std::clamp((contact_x-205)*.0012f,-.10f,.10f)*attention;
        ty=std::clamp((230-contact_y)*.0016f,-.105f,.095f)*attention;
        pitch=.06f*attention;
        if(group>=3) {
          float patient=ease(.17f,.65f,contact_age);tx*=1-patient;ty*=1-patient;
          roll=.07f*hold(.40f,.85f,1.40f,2.18f,contact_age);
        }
      } else {
        float attention=ease(mode==Mode::Flick?.14f:.08f,mode==Mode::Flick?.41f:.34f,contact_age);
        if(!down)attention*=1-ease(.05f,mode==Mode::Flick?1.36f:.66f,now-released);
        if(mode==Mode::Hold && down)attention*=1-ease(1.10f,1.55f,contact_age);
        tx=std::clamp((last_x-205)*.0014f,-.12f,.12f)*attention;
        ty=std::clamp((250-last_y)*.0016f,-.105f,.095f)*attention;
      }
      pose.mouth=mouthAt();
      blink=hold(0,.12f,.20f,.42f,now-blink_at);
      if(!down && now>std::max(end_at,blink_at+.42f)) {mode=Mode::Idle;quiet_since=now;next_glance=now+8;}
    } else {
      bool shaking=liquid.recovery.shaking;
      bool moving=motion.valid&&(motion.linear.length()>.065f || motion.shake.length()>.055f);
      if(moving)motion_last=now;
      if(shaking && mode!=Mode::Shake) {mode=Mode::Shake;started=now;released=-20;oval(.14f,.17f,forever);}
      else if(!shaking && mode==Mode::Shake && released<0) {
        released=now;release(.45f,.26f,.61f,.50f);blink_at=now+.18f;
      }
      // A new shake or swing cancels recovery as soon as movement resumes.
      if(shaking && mode==Mode::Shake && released>=0) {started=now;released=-20;oval(0,.15f,forever);}
      if(moving && !shaking && !(mode==Mode::Shake && released<0) && (mode!=Mode::Swing || released>=0)) {
        mode=Mode::Swing;started=now;released=-20;oval(.20f,.18f,forever);
      }
      if(mode==Mode::Swing && released<0 && now-motion_last>.20f) {released=now;release(.12f,.26f,.52f,.50f);}
      if(mode==Mode::Swing || mode==Mode::Shake) {
        float attention=ease(.17f,.38f,now-started);
        if(released>=0)attention*=1-ease(.13f,.70f,now-released);
        if(mode==Mode::Swing)tx=std::clamp(liquid.x.p*.7f,-.12f,.12f)*attention;
        else {ty=-.035f*attention;if(released>=0)tx=.055f*std::sin((now-released)*5)*std::exp(-(now-released)*2.2f);}
        pose.mouth=mouthAt();blink=hold(0,.13f,.21f,.44f,now-blink_at);
        if(released>=0 && now>std::max(end_at,blink_at+.44f)){mode=Mode::Idle;quiet_since=now;next_glance=now+8;}
      } else {
        if(motion.valid) {
          if(!tilt_initialized){tilt_reference=motion.gravity;tilt_initialized=true;}
          if((motion.gravity-tilt_reference).length()>.14f && now-tilt_last>1.4f) {
            mode=Mode::Tilt;tilt_last=started=now;tilt_reference=motion.gravity;quiet_since=now;
          }
        }
        if(mode==Mode::Tilt) {
          float a=hold(.18f,.55f,1.30f,1.76f,now-started);
          tx=std::clamp(motion.gravity.x*.26f,-.11f,.11f)*a;ty=-.016f*a;
          if(now-started>2.1f){mode=Mode::Idle;quiet_since=now;next_glance=now+10;}
        }
        pose.mouth=mix(pose.mouth,{},1-std::exp(-dt*12));
      }
    }
    // Voice owns one mouth target while speaking; interactions keep their
    // physics/attention but cannot fight the speech pose. No delayed replay.
    if(was_speaking && !speaking) {
      if(!down){cancelReaction();pose.mouth={};}
      else {mode=Mode::Hold;oval(0,.18f,forever);}
    }
    was_speaking=speaking;
    reaction_mouth=pose.mouth;
    voice_mix+=((speaking?1.f:0.f)-voice_mix)*(1-std::exp(-dt/ .10f));
    pose.mouth=mix(pose.mouth,{0,.30f+.70f*std::clamp(speech,0.f,1.f)},voice_mix);
    pose.quiet=mode==Mode::Idle && !speaking;
    if(pose.quiet) {
      if(now>=next_glance) {
        glance_at=now;seed=seed*1664525u+1013904223u;next_glance=now+12+float(seed%1000)*.01f;
      }
      float glance=hold(0,.42f,.94f,1.50f,now-glance_at);
      tx=(seed&1?-.047f:.047f)*glance;ty=.009f*glance;roll=.009f*glance;
    } else if(mode!=Mode::Tilt)glance_at=-20;
    pose.gaze_x+=(tx-pose.gaze_x)*(1-std::exp(-dt*10));
    pose.gaze_y+=(ty-pose.gaze_y)*(1-std::exp(-dt*10));
    pose.pitch=pitch;pose.roll=roll;pose.blink=blink;
    pose.auto_blink=pose.quiet || speaking || mode==Mode::Tilt;
  }
};
}
