#include "scout.h"
#include "json_storage.h"
#include "esp_timer.h"
#include "esp_private/esp_clk.h"
#include <cstring>
#include <cstdio>

static esp_pm_lock_handle_t render_lock;
void power_init() {
  esp_pm_config_t config={};
  config.max_freq_mhz=240;config.min_freq_mhz=80;
  // Retain USB and software motion/touch detection. This is responsive
  // standby, not deep sleep; radio/driver locks can temporarily raise clocks.
  config.light_sleep_enable=false;
  ESP_ERROR_CHECK(esp_pm_configure(&config));
  ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"avatar",&render_lock));
  ESP_ERROR_CHECK(esp_pm_lock_acquire(render_lock));
}
esp_pm_lock_handle_t power_render_lock() {return render_lock;}

// Local diagnostic only. Runtime comes from the microsecond timer, so DFS
// does not distort it. Compare two snapshots to measure each task and core.
cJSON *power_task_stats() {
  JsonStorage::External storage;
  constexpr unsigned capacity=40;
  auto *tasks=(TaskStatus_t*)heap_caps_calloc(capacity,sizeof(TaskStatus_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  cJSON *result=cJSON_CreateObject();
  if(!tasks)return result;
  configRUN_TIME_COUNTER_TYPE total=0;
  unsigned count=uxTaskGetSystemState(tasks,capacity,&total);
  cJSON_AddNumberToObject(result,"uptime_us",esp_timer_get_time());
  cJSON_AddNumberToObject(result,"total_runtime_us",total);
  cJSON_AddNumberToObject(result,"cpu_mhz",esp_clk_cpu_freq()/1000000);
  char *profile=nullptr;size_t profile_size=0;
  if(FILE *stream=open_memstream(&profile,&profile_size)) {
    esp_pm_dump_locks(stream);fclose(stream);
    if(profile) {cJSON_AddStringToObject(result,"clock_profile",profile);free(profile);}
  }
  cJSON *list=cJSON_AddArrayToObject(result,"tasks");
  for(unsigned i=0;i<count;i++) {
    auto *task=cJSON_CreateObject();cJSON_AddItemToArray(list,task);
    cJSON_AddStringToObject(task,"name",tasks[i].pcTaskName);
    cJSON_AddNumberToObject(task,"id",tasks[i].xTaskNumber);
    cJSON_AddNumberToObject(task,"runtime_us",tasks[i].ulRunTimeCounter);
    cJSON_AddNumberToObject(task,"state",tasks[i].eCurrentState);
  }
  free(tasks);return result;
}
