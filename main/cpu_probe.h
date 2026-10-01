#pragma once
#include "cJSON.h"
#include "esp_ipc.h"
#include "esp_timer.h"
#include "xtensa_perfmon_access.h"
#include "xtensa_perfmon_masks.h"

// Development-only, whole-core counters, including tasks and interrupts.
// No printf in the audio protocol. IPC callbacks only touch local counters;
// they never wait, allocate, render or hold the remote CPU during sampling.
namespace CpuProbe {
struct Event {const char* name;uint16_t select,mask;};
inline constexpr Event events[]={
 {"instructions",XTPERF_CNT_INSN,XTPERF_MASK_INSN_ALL},
 {"data_stall",XTPERF_CNT_D_STALL,XTPERF_MASK_D_STALL_ALL},
 {"instruction_stall",XTPERF_CNT_I_STALL,XTPERF_MASK_I_STALL_ALL},
 {"data_busy",XTPERF_CNT_D_STALL,XTPERF_MASK_D_STALL_BUSY},
 {"store_full",XTPERF_CNT_D_STALL,XTPERF_MASK_D_STALL_STORE_BUF_FULL},
 {"register_hold",XTPERF_CNT_BUBBLES,XTPERF_MASK_BUBBLES_R_HOLD_REG_DEP},
 {"waiti",XTPERF_CNT_BUBBLES,XTPERF_MASK_BUBBLES_WAITI},
};
struct Sample {int event;uint32_t cycles,count;int64_t begin,end;bool overflow;};
inline Sample samples[2]={};
inline bool active=false;
inline void begin(void* arg) {
 auto& s=*static_cast<Sample*>(arg);const auto& e=events[s.event];
 xtensa_perfmon_stop();
 xtensa_perfmon_init(0,XTPERF_CNT_CYCLES,XTPERF_MASK_CYCLES,0,6);
 xtensa_perfmon_init(1,e.select,e.mask,0,6);
 s.begin=esp_timer_get_time();xtensa_perfmon_start();
}
inline void end(void* arg) {
 auto& s=*static_cast<Sample*>(arg);xtensa_perfmon_stop();s.end=esp_timer_get_time();
 s.cycles=xtensa_perfmon_value(0);s.count=xtensa_perfmon_value(1);
 s.overflow=xtensa_perfmon_overflow(0)!=ESP_OK || xtensa_perfmon_overflow(1)!=ESP_OK;
}
inline cJSON* start(int event) {
 auto* o=cJSON_CreateObject();
 if(active || event<0 || event>=int(sizeof(events)/sizeof(events[0]))) {
  cJSON_AddStringToObject(o,"error",active?"Counters already running":"Invalid event");return o;
 }
 for(int core=0;core<2;core++) {
  samples[core]={};samples[core].event=event;
  if(esp_ipc_call_blocking(core,begin,&samples[core])!=ESP_OK) {
   for(int n=0;n<core;n++)esp_ipc_call_blocking(n,end,&samples[n]);
   cJSON_AddStringToObject(o,"error","Cannot start counters");return o;
  }
 }
 active=true;cJSON_AddStringToObject(o,"event",events[event].name);return o;
}
inline cJSON* stop() {
 auto* o=cJSON_CreateObject();
 if(!active){cJSON_AddStringToObject(o,"error","Counters not running");return o;}
 bool valid=true;
 for(int core=0;core<2;core++) {
  if(esp_ipc_call_blocking(core,end,&samples[core])!=ESP_OK)valid=false;
 }
 active=false;auto* list=cJSON_AddArrayToObject(o,"cores");
 cJSON_AddStringToObject(o,"event",events[samples[0].event].name);
 for(int core=0;core<2;core++) {
  const auto& s=samples[core];auto* c=cJSON_CreateObject();
  cJSON_AddNumberToObject(c,"core",core);cJSON_AddNumberToObject(c,"cycles",s.cycles);
  cJSON_AddNumberToObject(c,"count",s.count);cJSON_AddNumberToObject(c,"elapsed_us",double(s.end-s.begin));
  cJSON_AddBoolToObject(c,"overflow",s.overflow);cJSON_AddItemToArray(list,c);
  // Reject captures long enough to wrap even if an overflow flag is lost.
  valid=valid && !s.overflow && s.end>s.begin && s.end-s.begin<10000000;
 }
 cJSON_AddBoolToObject(o,"valid",valid);return o;
}
}
