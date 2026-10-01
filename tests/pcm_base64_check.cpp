#include "pcm_base64.h"
#include "mbedtls/base64.h"
#include <array>
#include <cassert>
#include <cstring>
#include <random>
#include <string>
#include <cstdio>

int main() {
 std::mt19937 rng(0x913a521u);size_t checks=0;
 auto check=[&](const std::string& s,size_t capacity) {
  std::array<uint8_t,656> a,b;a.fill(0xa7);b.fill(0xa7);
  size_t na=0,nb=0;
  int old=mbedtls_base64_decode(b.data()+8,capacity,&nb,(const uint8_t*)s.data(),s.size());
  bool fast=PcmBase64::decode(a.data()+8,capacity,na,s.data(),s.size());
  if(fast){assert(old==0 && na==nb);assert(!memcmp(a.data()+8,b.data()+8,na));}
  else {
   int result=mbedtls_base64_decode(a.data()+8,capacity,&na,(const uint8_t*)s.data(),s.size());
   assert(result==old);if(!result)assert(na==nb && !memcmp(a.data()+8,b.data()+8,na));
  }
  for(int i=0;i<8;i++)assert(a[i]==0xa7);
  for(size_t i=8+capacity;i<a.size();i++)assert(a[i]==0xa7);
  checks++;
 };
 for(size_t n=0;n<=640;n++)for(int repeat=0;repeat<20;repeat++) {
  std::array<uint8_t,640> raw;for(auto& v:raw)v=rng();
  std::array<char,873> got,reference;got.fill('!');reference.fill('!');
  size_t ng=0,nr=0;
  assert(PcmBase64::encode(got.data()+8,857,ng,raw.data(),n));
  assert(!mbedtls_base64_encode((uint8_t*)reference.data()+8,857,&nr,raw.data(),n));
  assert(ng==nr && !memcmp(got.data()+8,reference.data()+8,ng));
  assert(got[8+ng]==0);
  for(int i=0;i<8;i++)assert(got[i]=='!' && got[got.size()-1-i]=='!');
  assert(!PcmBase64::encode(got.data()+8,ng,ng,raw.data(),n));
  std::string encoded(reference.data()+8,nr);check(encoded,n);check(encoded,n?n-1:0);
  if(!encoded.empty()) {
   auto spaced=encoded;spaced.insert(spaced.size()/4*4,"\r\n");check(spaced,640);
   auto altered=encoded;altered[rng()%altered.size()]=char(rng());check(altered,640);
  }
 }
 const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/= \r\n\t!";
 for(int i=0;i<100000;i++) {
  std::string s(rng()%857,'A');
  for(auto& c:s)c=(i&1)?char(rng()):alphabet[rng()%(sizeof(alphabet)-1)];
  check(s,rng()%641);
 }
 for(auto s:{"","A","AA","AAA","AAAA","AA==","AB==","AAA=","AAB=","====","A===","AA=A","AA==AA==","AAAA ","AA==\n","AAAA\r\nAAAA","AAAA\t","AAAA AA=="})check(s,640);
 puts("Canonical 0..640-byte PCM encodings match actual SDK mbedTLS; decoder/fallback matches including malformed input and undersized buffers.");
 printf("%zu decoder cases; output canaries intact.\n",checks);
}
