// Build as a host executable, or include in a temporary S3 diagnostic build.
// The archived 0.9.13 boot diagnostic executed this same corpus on the S3.
#include "a3d/a3d_math.h"
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <initializer_list>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
static void vTaskDelay(int) {}
#endif
static void check_integer_rounding() {
  // Hardware-only diagnostic. Directed-rounding opcodes must match the
  // previous C++ implementation for finite, representable int results.
  uint32_t seed=0x3ad416e2;unsigned checked=0,failures=0;
  auto check=[](float v) {
    const int i=int(v),floor=v<float(i)?i-1:i,ceil=v>float(i)?i+1:i;
    return a3d::iFloor(v)==floor && a3d::iCeil(v)==ceil;
  };
  for(unsigned k=0;k<500000;k++) {
    seed=1664525u*seed+1013904223u;float v;memcpy(&v,&seed,4);
    if(std::isfinite(v) && v>=-2147483648.f && v<2147483648.f) {checked++;failures+=!check(v);}
    if((k&4095)==0)vTaskDelay(1);
  }
  for(int i=-2048;i<=2048;i++)for(float v:{float(i),std::nextafter(float(i),-INFINITY),std::nextafter(float(i),INFINITY)}) {checked++;failures+=!check(v);}
  printf("ROUNDING checked=%u failures=%u\n",checked,failures);
  assert(!failures);
}
#ifndef ESP_PLATFORM
int main() {check_integer_rounding();}
#endif
