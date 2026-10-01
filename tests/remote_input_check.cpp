#include "remote_input.h"
#include <cassert>
int main() {
  RemoteInput::History h;RemoteInput::Edge e[16];
  assert(h.copy(0,e)==0&&!h.overflow(0));
  h.push(1000,10,20,true,false,false);h.push(1100,10,20,false,false,false);
  assert(h.copy(0,e)==2&&e[0].down&&!e[1].down);
  assert(h.copy(1,e)==1&&e[0].seq==2&&h.copy(2,e)==0);
  for(int i=0;i<30;i++)h.push(1200+i,15,25,i%2,true,i==0);
  assert(h.overflow(0)&&h.copy(0,e)==16&&e[0].seq==17&&e[15].seq==32);
  assert(!h.overflow(16)&&h.overflow(33));
  assert(e[0].simulated&&e[0].x==15&&e[0].y==25);
}
