#pragma once
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "cJSON.h"
#include <cstring>

namespace CopyProbe {
using Word=uint32_t __attribute__((__may_alias__));
using Fn=void(*)(Word*,const Word*,size_t);
static void IRAM_ATTR __attribute__((noinline)) scalar(Word* out,const Word* in,size_t n) {
  for(size_t i=0;i<n;i++)out[i]=in[i];
}
static void IRAM_ATTR __attribute__((noinline)) unrolled(Word* out,const Word* in,size_t n) {
  #pragma GCC unroll 8
  for(size_t i=0;i<n;i++)out[i]=in[i];
}
static void IRAM_ATTR __attribute__((noinline)) library(Word* out,const Word* in,size_t n) {
  void*(*volatile copy)(void*,const void*,size_t)=memcpy;
  copy(out,in,n*4);
}
static void IRAM_ATTR __attribute__((noinline)) aligned(Word* out,const Word* in,size_t n) {
  memcpy(__builtin_assume_aligned(out,4),__builtin_assume_aligned(in,4),n*4);
}
// Read-only performance diagnostic: private buffers, no display/DMA ownership.
inline cJSON* run() {
  auto* result=cJSON_CreateObject();
  auto* src=(Word*)heap_caps_malloc(8192,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  auto* dst=(Word*)heap_caps_malloc(8192*24+64,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!src||!dst){free(src);free(dst);cJSON_AddStringToObject(result,"error","Insufficient probe buffers");return result;}
  for(int i=0;i<2048;i++)src[i]=uint32_t(i*1973)^0x725e3aa1u;
  esp_pm_lock_handle_t boost=nullptr;
  esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"copy_probe",&boost);
  if(boost)esp_pm_lock_acquire(boost);
  const char* names[]={"scalar","unrolled8","libc","aligned_libc"};
  Fn functions[]={scalar,unrolled,library,aligned};
  bool valid=true;
  // Rotating across a buffer larger than D-cache avoids measuring one hot
  // cache line. Alternate 4-byte offsets reproduce paired native row starts.
  for(int kind=0;kind<4;kind++) {
    int64_t start=esp_timer_get_time();
    for(int i=0;i<240;i++)functions[kind](dst+(i%24)*2048+(i&1),src,2048);
    cJSON_AddNumberToObject(result,names[kind],double(esp_timer_get_time()-start));
    for(int offset=0;offset<16;offset++)for(int n: {1,2,7,8,17,205,738,2048}) {
      dst[offset+n]=0x9aeebb11;functions[kind](dst+offset,src,n);
      valid=valid && !memcmp(dst+offset,src,n*4) && dst[offset+n]==0x9aeebb11;
    }
  }
  cJSON_AddBoolToObject(result,"valid",valid);cJSON_AddNumberToObject(result,"bytes_per_method",8192*240);
  if(boost){esp_pm_lock_release(boost);esp_pm_lock_delete(boost);}
  free(src);free(dst);return result;
}
}
