#include "touch_history.h"
#include "ditto_behavior.h"
#include <cassert>
#include <cstdio>
#include <vector>
using TouchHistory::Event;
struct Simulation {
  Blob::Physics p;TouchHistory::Cursor cursor;DittoBehavior::Controller behavior;
  TouchHistory::Queue<> q;
  void frame(int64_t now) {
    auto advance=[&](float dt,const Blob::Touch& t) {
      p.step(dt,t);behavior.step(dt,t,p.impact_hit,{},p.liquid,false,false,0);
    };
    auto pick=[&](const Blob::Touch& t){p.setImpact(t.press,true,0,0,0,0,.5f,1);};
    cursor.begin(now,.1f);Event e;while(q.pop(now,e))cursor.consume(e,advance,pick);cursor.finish(now,advance);
  }
};
int main() {
  Blob::Physics held;Blob::Touch t{true,205,280,205,280,1};held.setImpact(1,true);held.step(.001f,t);
  t.x=290;float target=85.f/145*.86f,t50=-1,t90=-1;
  for(int i=1;i<=500;i++){held.step(.001f,t);if(t50<0&&held.pull_x.p>=target*.5f)t50=i;if(t90<0&&held.pull_x.p>=target*.9f)t90=i;}
  assert(t50<=22 && t90<=50);
  printf("Held skin 50%% %.0f ms; 90%% %.0f ms\n",t50,t90);
  // Same 10 ms acquisition stream at every phase of a 38 ms renderer. A swipe
  // and a round trip must survive even when they finish between two renders.
  for(int duration:{10,20,30,60,100})for(int phase=0;phase<38;phase++) {
    Simulation s;std::vector<Event> events;
    Blob::Touch a{true,205,280,205,280,1};events.push_back({a,100000});
    for(int ms=5;ms<=duration;ms+=5){a.x=205+95*ms/duration;events.push_back({a,(100+ms)*1000});}
    a.down=false;events.push_back({a,(101+duration)*1000});
    size_t i=0;float peak=0;
    for(int now=phase;now<800;now+=38) {
      while(i<events.size()&&events[i].time_us<=now*1000){s.q.push(events[i].touch,events[i].time_us);i++;}
      s.frame(now*1000);peak=std::max(peak,s.p.stretch());
    }
    assert(s.p.seen_press==1 && s.p.tap_count==0 && !s.p.grabbed && peak>.035f);
    assert(s.q.overflows==0);
  }
  Simulation circle;Blob::Touch a{true,205,280,205,280,1};circle.q.push(a,10000);
  for(int i=1;i<=6;i++) {a.x=205+int(50*std::sin(i*1.047f));a.y=280+int(50*(1-std::cos(i*1.047f)));circle.q.push(a,10000+i*3000);}
  a.down=false;circle.q.push(a,30000);circle.frame(38000);
  assert(circle.p.tap_count==0 && std::abs(circle.p.roll.v)>.001f);
  // Queue overload drops interior moves first, reports the loss, and retains
  // the release. Repeated stationary samples do not crowd out real input.
  TouchHistory::Queue<8> q;a={true,205,280,205,280,1};q.push(a,1);
  for(int i=0;i<1000;i++)q.push(a,i+2);assert(q.count==1&&q.coalesced==1000);
  for(int i=0;i<30;i++){a.x++;q.push(a,2000+i);}
  a.down=false;q.push(a,2100);assert(q.overflows>0&&q.count==8);
  Event e;assert(!q.pop(0,e));assert(q.pop(1,e)&&e.touch.down&&e.touch.x==205);
  while(q.pop(3000,e)){}assert(!e.touch.down);
  // Multiple full taps in one frame retain all identities and classifications.
  Simulation taps;
  for(int i=1;i<=3;i++){a={true,205,280,205,280,uint32_t(i)};taps.q.push(a,i*8000);a.down=false;taps.q.push(a,i*8000+4000);}
  taps.frame(38000);assert(taps.p.tap_count==3&&taps.behavior.group==3);
  // A long render pause never integrates unbounded time, and zero-time event
  // transitions cannot poison the material or lose a final release.
  taps.frame(10000000);assert(std::isfinite(taps.p.activity())&&!taps.p.grabbed);
  puts("190 swipe phases, within-frame circle/reversals, repeated taps, deduplication, cutoff, bounded overflow and release preservation passed.");
}
