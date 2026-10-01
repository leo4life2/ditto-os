#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace BleControl {
constexpr size_t Size=44;
struct Command {bool dictating=false,audio=false;uint32_t sequence=0;char client[37]={};const char* kind=nullptr;};
inline bool decode(const uint8_t* p,size_t n,Command& c) {
 if(n!=Size||(p[0]!=1&&p[0]!=2)||p[1]>(p[0]==1?1:3)||p[2]>3||p[3]!=0)return false;
 c.dictating=p[1]&1;c.audio=p[0]==2&&(p[1]&2);c.sequence=uint32_t(p[4])|(uint32_t(p[5])<<8)|(uint32_t(p[6])<<16)|(uint32_t(p[7])<<24);
 if((p[2]==0)!=(c.sequence==0))return false;
 for(unsigned i=0;i<36;i++){
  const bool dash=i==8||i==13||i==18||i==23;
  if(dash?p[8+i]!='-':!((p[8+i]>='0'&&p[8+i]<='9')||(p[8+i]>='A'&&p[8+i]<='F')||(p[8+i]>='a'&&p[8+i]<='f')))return false;
 }
 memcpy(c.client,p+8,36);c.client[36]=0;
 c.kind=p[2]==1?"end":p[2]==2?"enter":p[2]==3?"undo":nullptr;
 return true;
}
}
