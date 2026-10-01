#pragma once
#include <cstdint>
namespace RemoteInput {
struct Edge { uint32_t seq=0; int64_t us=0; int16_t x=0,y=0; bool down=false,simulated=false,woke=false; };
struct History {
  static constexpr unsigned capacity=16;
  Edge edges[capacity]{};
  uint32_t sequence=0;
  bool simulated=false;
  void push(int64_t us,int x,int y,bool down,bool synthetic,bool woke) {
    edges[sequence%capacity]={sequence+1,us,int16_t(x),int16_t(y),down,synthetic,woke};
    sequence++;
  }
  bool overflow(uint32_t after) const {return after>sequence || sequence-after>capacity;}
  unsigned copy(uint32_t after,Edge *out) const {
    unsigned n=0;uint32_t begin=sequence>capacity?sequence-capacity:0;
    if(after>begin && after<=sequence)begin=after;
    for(uint32_t i=begin;i<sequence;i++)out[n++]=edges[i%capacity];
    return n;
  }
};
}
