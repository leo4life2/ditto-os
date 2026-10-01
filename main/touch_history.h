#pragma once
#include "blob_physics.h"
#include <cstddef>

// Acquired samples survive render stalls. Callers provide synchronization and
// storage (PSRAM on the board); there is no allocation on the input path.
namespace TouchHistory {
struct Event { Blob::Touch touch; int64_t time_us=0; };
template<size_t Capacity=64> struct Queue {
  static_assert(Capacity>=4);
  Event items[Capacity]{};
  size_t count=0;
  uint32_t enqueued=0,coalesced=0,overflows=0,high_water=0;
  Blob::Touch latest;
  bool have_latest=false;
  void push(const Blob::Touch& t,int64_t time) {
    if(have_latest && t.press==latest.press && t.down==latest.down &&
        (!t.down || (t.x==latest.x && t.y==latest.y))) {coalesced++;return;}
    latest=t;have_latest=true;
    if(count==Capacity) {
      // Prefer losing one interior move, never a down/up boundary. If an
      // entire queue consists of transitions, discard its oldest event:
      // press IDs retain touchdown anchors and the newest release still wins.
      size_t victim=0;
      for(size_t i=1;i+1<count;i++)
        if(items[i-1].touch.down && items[i].touch.down && items[i+1].touch.down &&
            items[i-1].touch.press==items[i].touch.press &&
            items[i].touch.press==items[i+1].touch.press) {victim=i;break;}
      for(size_t i=victim+1;i<count;i++)items[i-1]=items[i];
      count--;overflows++;
    }
    items[count++]={t,time};enqueued++;high_water=std::max(high_water,uint32_t(count));
  }
  bool pop(int64_t cutoff,Event& e) {
    if(!count || items[0].time_us>cutoff)return false;
    e=items[0];for(size_t i=1;i<count;i++)items[i-1]=items[i];count--;return true;
  }
};

// Replay only input/physics, not the expensive mesh deformation. Move samples
// are interval endpoints; a release integrates the last held position before
// releasing. Thus even a whole gesture between two renders retains momentum.
struct Cursor {
  Blob::Touch current;
  int64_t time_us=0;
  void begin(int64_t cutoff,float budget_s) {
    time_us=std::max(time_us,cutoff-int64_t(std::clamp(budget_s,0.f,.1f)*1e6f));
  }
  template<class Advance,class Pick> void consume(const Event& e,Advance advance,Pick pick) {
    float dt=std::max<int64_t>(0,e.time_us-time_us)*1e-6f;
    if(e.touch.press!=current.press || !e.touch.down) {
      if(dt>0)advance(dt,current);
      if(e.touch.press!=current.press)pick(e.touch);
      advance(0,e.touch);
    } else advance(dt,e.touch);
    current=e.touch;time_us=std::max(time_us,e.time_us);
  }
  template<class Advance> void finish(int64_t cutoff,Advance advance) {
    advance(std::max<int64_t>(0,cutoff-time_us)*1e-6f,current);time_us=cutoff;
  }
};
}
