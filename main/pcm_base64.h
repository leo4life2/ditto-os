#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Bounded audio transport only, never keys or cryptographic material. The
// existing mbedTLS decoder remains the fallback for whitespace/other syntax.
namespace PcmBase64 {
inline constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
inline constexpr auto decode_table=[] {
 std::array<uint8_t,256> table{};
 for(auto& value:table)value=255;
 for(unsigned i=0;i<64;i++)table[uint8_t(alphabet[i])]=i;
 return table;
}();
inline bool decode(uint8_t* dst,size_t capacity,size_t& written,const char* src,size_t size) {
 written=0;
 if(size==0)return true;
 if(!src || !dst || size>856 || (size&3))return false;
 const size_t padding=src[size-1]=='='?(src[size-2]=='='?2:1):0;
 const size_t count=size/4*3-padding;
 if(count>capacity)return false;
 for(size_t i=0;i<size;i+=4) {
  const unsigned a=decode_table[uint8_t(src[i])],b=decode_table[uint8_t(src[i+1])];
  const bool last=i+4==size;
  const unsigned c=last && padding==2?0:decode_table[uint8_t(src[i+2])];
  const unsigned d=last && padding?0:decode_table[uint8_t(src[i+3])];
  if((a|b|c|d)>63)return false;
  const uint32_t word=(a<<18)|(b<<12)|(c<<6)|d;
  *dst++=uint8_t(word>>16);
  if(!last || padding<2)*dst++=uint8_t(word>>8);
  if(!last || !padding)*dst++=uint8_t(word);
 }
 written=count;return true;
}
inline bool encode(char* dst,size_t capacity,size_t& written,const uint8_t* src,size_t size) {
 written=0;
 if(size>640 || !dst || (size && !src))return false;
 const size_t count=(size+2)/3*4;
 if(capacity<=count)return false;
 size_t i=0;char* out=dst;
 for(;i+3<=size;i+=3) {
  const uint32_t word=(uint32_t(src[i])<<16)|(uint32_t(src[i+1])<<8)|src[i+2];
  *out++=alphabet[word>>18];*out++=alphabet[(word>>12)&63];
  *out++=alphabet[(word>>6)&63];*out++=alphabet[word&63];
 }
 if(i<size) {
  const uint32_t word=(uint32_t(src[i])<<16)|(i+1<size?uint32_t(src[i+1])<<8:0);
  *out++=alphabet[word>>18];*out++=alphabet[(word>>12)&63];
  *out++=i+1<size?alphabet[(word>>6)&63]:'=';*out++='=';
 }
 *out=0;written=count;return true;
}
}
