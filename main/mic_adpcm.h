#pragma once
#include <cstdint>
#include <cstddef>
#include <algorithm>
// Independent 20 ms IMA ADPCM blocks. Every block carries its predictor and
// step index: packet loss cannot corrupt any subsequent block.
namespace MicADPCM {
constexpr size_t Samples=320,Bytes=172;
constexpr int steps[]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
constexpr int adjustments[]={-1,-1,-1,-1,2,4,6,8};
inline int apply(int code,int& predictor,int& index){
 int step=steps[index],diff=step>>3;if(code&1)diff+=step>>2;if(code&2)diff+=step>>1;if(code&4)diff+=step;
 predictor=std::clamp(predictor+((code&8)?-diff:diff),-32768,32767);index=std::clamp(index+adjustments[code&7],0,88);return predictor;
}
inline void encode(const int16_t* pcm,uint8_t* out,uint32_t seq,int& index,bool muted=false){
 std::fill(out,out+Bytes,0);out[0]=1;out[1]=muted?1:0;out[2]=16;
 for(int i=0;i<4;i++)out[4+i]=uint8_t(seq>>(i*8));
 int predictor=muted?0:pcm[0];out[8]=uint8_t(predictor);out[9]=uint8_t(predictor>>8);out[10]=uint8_t(index);
 for(size_t i=1;i<Samples;i++){
  int diff=(muted?0:int(pcm[i]))-predictor,code=diff<0?8:0;diff=std::abs(diff);int step=steps[index];
  if(diff>=step){code|=4;diff-=step;}if(diff>=(step>>1)){code|=2;diff-=step>>1;}if(diff>=(step>>2))code|=1;
  apply(code,predictor,index);out[12+(i-1)/2]|=uint8_t(code<<(((i-1)&1)*4));
 }
}
inline bool decode(const uint8_t* data,size_t n,int16_t* pcm,uint32_t& sequence){
 if(n!=Bytes||data[0]!=1||data[1]>1||data[2]!=16||data[3]||data[10]>88||data[11])return false;
 sequence=uint32_t(data[4])|(uint32_t(data[5])<<8)|(uint32_t(data[6])<<16)|(uint32_t(data[7])<<24);
 int predictor=int16_t(uint16_t(data[8])|(uint16_t(data[9])<<8)),index=data[10];pcm[0]=predictor;
 for(size_t i=1;i<Samples;i++)pcm[i]=apply((data[12+(i-1)/2]>>(((i-1)&1)*4))&15,predictor,index);
 if(data[1])std::fill(pcm,pcm+Samples,0);
 return true;
}
}
