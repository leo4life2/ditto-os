#include "power_log.h"
#include "power_journal.h"
#include "scout.h"
#include "usb_device.h"
#include "ble_remote.h"
#include "json_storage.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_private/esp_clk.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include <ctime>
#include <cstdio>
#include <new>
namespace {
using namespace PowerJournal;
struct Flash:Storage {
 const esp_partition_t* p=nullptr;
 bool read(size_t o,void* b,size_t n)override{return p && esp_partition_read(p,o,b,n)==ESP_OK;}
 bool write(size_t o,const void* b,size_t n)override{return p && esp_partition_write(p,o,b,n)==ESP_OK;}
 bool erase(size_t o,size_t n)override{return p && esp_partition_erase_range(p,o,n)==ESP_OK;}
} flash;
Journal* journal=nullptr;SemaphoreHandle_t mutex=nullptr;
constexpr unsigned QueueSize=16,Tasks=40;
Record* queue=nullptr;unsigned head=0,queued=0;uint32_t dropped=0,errors=0,boot=0;
TaskStatus_t* tasks=nullptr;
struct Prev {UBaseType_t id;uint32_t runtime;};
Prev* previous=nullptr;unsigned previous_count=0;
Data accumulator;
State* last_state_ptr=nullptr;
#define last_state (*last_state_ptr)
int64_t last_tick=0,last_cpu=0,record_at=0,last_write_attempt=0;
bool last_audio=false,last_ble=false,initialized=false;
char device_id[13]={};const char* error="not initialized";
uint32_t min_internal=UINT32_MAX;
void runtime(int64_t now){
 if(!tasks)return;
 configRUN_TIME_COUNTER_TYPE total=0;
 unsigned n=uxTaskGetSystemState(tasks,Tasks,&total);
 if(!n){previous_count=0;last_cpu=now;return;}
 if(last_cpu && previous_count){
  uint32_t elapsed=uint32_t((now-last_cpu)/1000);accumulator.cpu_elapsed_ms+=elapsed;
  for(unsigned i=0;i<n;i++)for(unsigned j=0;j<previous_count;j++)if(tasks[i].xTaskNumber==previous[j].id){
   // The configured counters wrap every ~71 minutes; frequent unsigned
   // subtraction retains correct deltas over a full discharge cycle.
   uint32_t delta=uint32_t(tasks[i].ulRunTimeCounter)-previous[j].runtime;
   const char* name=tasks[i].pcTaskName;
   if(!strcmp(name,"IDLE0"))accumulator.idle0_us+=delta;
   else if(!strcmp(name,"IDLE1"))accumulator.idle1_us+=delta;
   else if(!strcmp(name,"scout_ui")||!strcmp(name,"a3d_job")||!strcmp(name,"display_present"))accumulator.render_us+=delta;
   else if(!strcmp(name,"services")||!strcmp(name,"touch_input"))accumulator.services_us+=delta;
   else if(!strcmp(name,"mic")||!strcmp(name,"speaker")||!strcmp(name,"usb_device"))accumulator.audio_us+=delta;
   else if(!strcmp(name,"nimble_host")||!strcmp(name,"btController")||!strcmp(name,"wifi"))accumulator.radio_us+=delta;
   break;
  }
 }
 for(unsigned i=0;i<n;i++)previous[i]={tasks[i].xTaskNumber,uint32_t(tasks[i].ulRunTimeCounter)};
 previous_count=n;last_cpu=now;
}
void enqueue(const State& s,int64_t now,uint32_t extra){
 Record r{};r.d=accumulator;r.d.boot=boot;r.d.epoch=time(nullptr);r.d.uptime_ms=now/1000;
 r.d.flags=extra|(s.battery_external?External:0)|(s.battery_present?Present:0)|
  (s.battery_status_valid?ValidBattery:0)|(s.sleeping?Sleeping:0)|(s.render_parked?Parked:0)|
  (s.usb_connected?USB:0)|(s.bluetooth_connected?BLE:0)|(s.battery_charging?Charging:0)|
  (r.d.epoch>=1700000000?ClockValid:0)|(s.battery_voltage_valid?VoltageValid:0);
 r.d.percent=s.battery_percent;r.d.millivolts=s.battery_voltage_valid?uint16_t(s.battery_v*1000+0.5f):0;
 r.d.min_internal=min_internal==UINT32_MAX?0:min_internal;r.d.reset_reason=esp_reset_reason();
 snprintf(r.d.firmware,sizeof(r.d.firmware),"%.15s",esp_app_get_description()->version);
 if(queued==QueueSize){head=(head+1)%QueueSize;queued--;dropped++;}
 r.d.dropped_records=dropped;queue[(head+queued)%QueueSize]=r;queued++;
 accumulator=Data{};record_at=now;min_internal=UINT32_MAX;
}
void add_json(cJSON* o,const Record& r){
 const auto& d=r.d;
#define N(k) cJSON_AddNumberToObject(o,#k,double(d.k))
 N(sequence);N(boot);N(epoch);N(uptime_ms);N(flags);N(percent);N(millivolts);
 N(span_ms);N(awake_ms);N(sleep_ms);N(battery_ms);N(unknown_power_ms);N(usb_ms);N(ble_ms);N(ble_available_ms);N(wifi_connected_ms);
 N(mic_awake_ms);N(audio_ms);N(dictating_ms);N(brightness_pct_ms);N(fast_cpu_ms);
 N(cpu_elapsed_ms);N(idle0_us);N(idle1_us);N(render_us);N(services_us);N(audio_us);N(radio_us);
 N(frames);N(physics_steps);N(contacts);N(min_internal);N(reset_reason);N(dropped_records);N(display_errors);N(audio_errors);
#undef N
 cJSON_AddStringToObject(o,"firmware",d.firmware);
}
}
void power_log_init(){
 mutex=xSemaphoreCreateMutex();
 flash.p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"storage");
 if(!flash.p || flash.p->size<PowerJournal::Bytes){error="storage partition unavailable";return;}
 auto alloc=[](size_t n){return heap_caps_calloc(1,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);};
 previous=(Prev*)alloc(Tasks*sizeof(Prev));void* state_memory=alloc(sizeof(State));last_state_ptr=state_memory?new(state_memory) State:nullptr;
 auto* index=(uint32_t*)alloc(Capacity*sizeof(uint32_t));queue=(Record*)alloc(QueueSize*sizeof(Record));
 tasks=(TaskStatus_t*)alloc(Tasks*sizeof(TaskStatus_t));void* memory=alloc(sizeof(Journal));
 if(!mutex||!index||!queue||!tasks||!memory||!previous||!last_state_ptr){error="diagnostic memory unavailable";return;}
 journal=new(memory)Journal(flash,index);
 if(!journal->open()){error="storage occupied, incompatible, or unreadable; nothing erased";return;}
 uint8_t mac[6]={};esp_efuse_mac_get_default(mac);
 snprintf(device_id,sizeof(device_id),"%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
 boot=esp_random();error="";initialized=true;
}
void power_log_tick(const State& s,bool may_write){
 if(!initialized)return;
 const int64_t now=esp_timer_get_time();
 if(now-last_tick<1000000)return;
 xSemaphoreTake(mutex,portMAX_DELAY);
 if(last_tick){
  uint32_t ms=uint32_t((now-last_tick)/1000);accumulator.span_ms+=ms;
  (last_state.sleeping?accumulator.sleep_ms:accumulator.awake_ms)+=ms;
  if(!last_state.battery_status_valid)accumulator.unknown_power_ms+=ms;
  else if(last_state.battery_present&&!last_state.battery_external)accumulator.battery_ms+=ms;
  if(last_state.usb_connected)accumulator.usb_ms+=ms;
  if(last_state.bluetooth_connected)accumulator.ble_ms+=ms;
  if(last_ble)accumulator.ble_available_ms+=ms;
  if(last_state.wifi_connected||last_state.wifi_connecting||last_state.wifi_scanning)accumulator.wifi_connected_ms+=ms;
  if(!last_state.audio_sleeping)accumulator.mic_awake_ms+=ms;
  if(last_audio||last_state.audio_stream)accumulator.audio_ms+=ms;
  if(last_state.remote_dictating)accumulator.dictating_ms+=ms;
  accumulator.brightness_pct_ms+=std::clamp(last_state.effective_brightness,0,100)*ms;
  if(esp_clk_cpu_freq()>80000000)accumulator.fast_cpu_ms+=ms;
  accumulator.frames+=s.presented_frames-last_state.presented_frames;
  accumulator.physics_steps+=s.physics_steps-last_state.physics_steps;
  accumulator.contacts+=s.touch_contacts-last_state.touch_contacts;
  accumulator.display_errors+=s.display_errors-last_state.display_errors;
  accumulator.audio_errors+=(s.audio_rx_errors-last_state.audio_rx_errors)+(s.audio_tx_errors-last_state.audio_tx_errors)+(s.audio_overflows-last_state.audio_overflows);
 }
 min_internal=std::min(min_internal,uint32_t(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
 if(now-last_cpu>=5000000)runtime(now);
 const bool changed=last_tick && s.battery_status_valid && last_state.battery_status_valid && s.battery_external!=last_state.battery_external;
 // Capture the unplug/replug boundary promptly, and keep one minute records
 // during unattended use. Flash is deferred while audio/dictation is active.
 if(!last_tick || changed || now-record_at>=60000000)enqueue(s,now,!last_tick?Start:changed?SourceChange:0);
 last_tick=now;last_state=s;last_audio=scout_usb_recording()||ble_remote_recording();last_ble=ble_remote_available();
 if(queued && may_write && !s.remote_dictating && now-last_write_attempt>=1000000){
  last_write_attempt=now;
  if(journal->append(queue[head])){head=(head+1)%QueueSize;queued--;}
  else errors++;
 }
 xSemaphoreGive(mutex);
}
cJSON* power_log_info(){
 JsonStorage::External external;auto* o=cJSON_CreateObject();
 if(mutex)xSemaphoreTake(mutex,portMAX_DELAY);
 cJSON_AddBoolToObject(o,"available",initialized);cJSON_AddStringToObject(o,"error",error);
 cJSON_AddStringToObject(o,"device_id",device_id);cJSON_AddNumberToObject(o,"schema",Version);
 cJSON_AddStringToObject(o,"measurement","voltage, fuel-gauge percentage and workload timing; no current/power meter");
 cJSON_AddNumberToObject(o,"record_interval_s",60);cJSON_AddNumberToObject(o,"capacity",Capacity);
 cJSON_AddNumberToObject(o,"records",journal?journal->count:0);cJSON_AddNumberToObject(o,"latest_sequence",journal?journal->latest:0);
 cJSON_AddNumberToObject(o,"invalid_records",journal?journal->bad:0);cJSON_AddNumberToObject(o,"queued",queued);
 cJSON_AddNumberToObject(o,"dropped",dropped);cJSON_AddNumberToObject(o,"write_errors",errors);
 cJSON_AddNumberToObject(o,"boot",boot);
 if(mutex)xSemaphoreGive(mutex);
 return o;
}
cJSON* power_log_read(unsigned after,unsigned limit){
 JsonStorage::External external;auto* o=cJSON_CreateObject();auto* list=cJSON_AddArrayToObject(o,"records");
 if(!initialized)return o;
 xSemaphoreTake(mutex,portMAX_DELAY);Record r;
 for(unsigned i=0;i<std::min(limit,8u)&&journal->after(after,r);i++){
  auto* item=cJSON_CreateObject();add_json(item,r);cJSON_AddItemToArray(list,item);after=r.d.sequence;
 }
 cJSON_AddNumberToObject(o,"next",after);cJSON_AddNumberToObject(o,"latest",journal->latest);
 xSemaphoreGive(mutex);return o;
}
