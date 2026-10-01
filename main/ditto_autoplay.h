#pragma once
#include "avatar_asset.h"
#include <cstdint>

// Scheduling only: no changes to mesh, expressions, body physics or sleep timer.
namespace DittoAutoplay {
inline constexpr int reactions[]={Avatar::Yes,Avatar::Celebrate,Avatar::Giggle,
    Avatar::Happy,Avatar::Surprise,Avatar::Wink,Avatar::Thinking};
// Wink gets 1.5x the eligible selection weight, while immediate repeats remain
// excluded. This favors the user's favorite without making it dominate.
inline constexpr int weights[]={2,2,2,2,2,3,2};
inline constexpr int choices=sizeof(reactions)/sizeof(reactions[0]);
static_assert(sizeof(weights)/sizeof(weights[0])==choices);
struct Decision {int clip=-1;bool cancel=false;};
struct Scheduler {
  uint32_t rng=1,seen_event=0,hello_count=0,reaction_count=0;
  int last_reaction=-1;
  int64_t hello_at=0;
  bool owns=false,reaction=false;
  explicit Scheduler(uint32_t seed=1):rng(seed?seed:1){}
  uint32_t random(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
  Decision tick(int64_t now,bool available,bool busy,uint32_t event,bool manual) {
    const bool fresh=event!=seen_event;seen_event=event;
    Decision d;
    if(manual)owns=false;
    if(!available) {d.cancel=owns;owns=false;hello_at=0;return d;}
    if(fresh && !manual) {
      int total=0;for(int j=0;j<choices;j++)if(j!=last_reaction)total+=weights[j];
      int ticket=int(random()%total),i=0;
      for(;i<choices;i++)if(i!=last_reaction){if(ticket<weights[i])break;ticket-=weights[i];}
      last_reaction=i;d.clip=reactions[i];reaction_count++;owns=true;reaction=true;hello_at=0;
      return d;
    }
    if(manual || busy) {hello_at=0;return d;}
    owns=false;
    if(!hello_at)hello_at=now+25000000+int64_t(random()%20000001);
    if(now>=hello_at) {d.clip=Avatar::Wave;hello_count++;owns=true;reaction=false;hello_at=0;}
    return d;
  }
};
}
