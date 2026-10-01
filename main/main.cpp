#include "ble_remote.h"
#include "usb_device.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "json_storage.h"
#include "wire_output.h"
#include "scout.h"
#include <cstring>
#include <atomic>
#include <cstdio>

static SemaphoreHandle_t output_mutex;
static constexpr size_t OUTPUT_BYTES=24*1024;
static char *output_buffer;
static constexpr size_t INPUT_BYTES=4096;
static char *input_buffer;
static StaticTask_t ui_task_control;
static constexpr size_t UI_STACK_BYTES=12288;
static StackType_t* ui_task_stack;
static std::atomic<const char*> startup_stage{"USB"};
static void startup_watch(void*) {
  // Repeat the current stage after USB enumeration; a failed peripheral must
  // not leave a blank screen with no clue how far initialization reached.
  for(;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    const char* stage=startup_stage.load();if(!stage)break;
    char message[200];
    int n=snprintf(message,sizeof(message),"SCOUT {\"event\":\"system.startup\",\"stage\":\"%s\",\"internal_free\":%u,\"internal_largest\":%u}\n",stage,
      unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
    if(n>0 && size_t(n)<sizeof(message) && xSemaphoreTake(output_mutex,pdMS_TO_TICKS(100))==pdTRUE) {
      scout_usb_write(message,n);xSemaphoreGive(output_mutex);
    }
  }
  vTaskDelete(nullptr);
}
static int write_output(const char* data,size_t size) {
  return scout_usb_write(data,size);
}
void emit(cJSON *ev) {
  if(!ev)return;
  xSemaphoreTake(output_mutex,portMAX_DELAY);
  JsonStorage::External storage;
  // Reserve framing inside the shared scratch area. A typical reply now
  // enters the driver as one item instead of prefix/body/newline separately.
  char* body=output_buffer?output_buffer+ScoutWire::PrefixSize:nullptr;
  bool buffered=body && cJSON_PrintPreallocated(ev,body,OUTPUT_BYTES-ScoutWire::PrefixSize-1,false);
  char* json=buffered?body:cJSON_PrintUnformatted(ev);
  cJSON_Delete(ev);
  if(json) {
    const size_t length=strlen(json);
    if(buffered) {
      memcpy(output_buffer,ScoutWire::Prefix,ScoutWire::PrefixSize);
      body[length]='\n';
      ScoutWire::frame(output_buffer,ScoutWire::PrefixSize+length+1,write_output);
    } else {
      ScoutWire::json(json,length,write_output);
      cJSON_free(json);
    }
  }
  xSemaphoreGive(output_mutex);
}
void emit_audio_input(const uint8_t* pcm,size_t size) {
  char packet[ScoutWire::AudioBytes];
  const size_t length=ScoutWire::audio_frame(packet,sizeof(packet),pcm,size);
  if(!length)return;
  xSemaphoreTake(output_mutex,portMAX_DELAY);
  ScoutWire::frame(packet,length,write_output);
  xSemaphoreGive(output_mutex);
}
void event_message(const char *type, const char *message) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "event", type);
  cJSON_AddStringToObject(o, "message", message);
  emit(o);
}
extern "C" void app_main() {
  vTaskPrioritySet(nullptr, 5);
  state_mutex = xSemaphoreCreateMutex();
  ui_mutex = xSemaphoreCreateMutex();
  output_mutex = xSemaphoreCreateMutex();
  // Install hooks before the first JSON value is created, including events.
  cJSON_Hooks hooks={JsonStorage::allocate,heap_caps_free};cJSON_InitHooks(&hooks);
  output_buffer=static_cast<char*>(heap_caps_malloc(OUTPUT_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  // Keep the full command capacity without reserving 4 KiB in the internal
  // main-task stack. Small parsed audio/control values still use fast RAM.
  input_buffer=static_cast<char*>(heap_caps_malloc(INPUT_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!input_buffer){event_message("system.error","Could not allocate command buffer");return;}
  power_init();
  scout_usb_init();
  xTaskCreatePinnedToCore(startup_watch,"startup_watch",2048,nullptr,3,nullptr,1);
  esp_log_level_set("*", ESP_LOG_WARN);
  startup_stage="board";
  board_init();
  // Keep the original display stack size, but reserve it before Wi-Fi/audio
  // fragment internal RAM into blocks too small to create the task later.
  ui_task_stack=static_cast<StackType_t*>(heap_caps_malloc(UI_STACK_BYTES,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  startup_stage="renderer allocation";
  ui_reserve_memory();
  startup_stage="services";
  services_init();
  startup_stage="brightness";
  board_brightness(snapshot().brightness);
  startup_stage="audio";
  board_audio_init();
  startup_stage="display task";
  if (!ui_task_stack || !xTaskCreateStaticPinnedToCore(ui_task,"scout_ui",UI_STACK_BYTES,nullptr,2,ui_task_stack,&ui_task_control,0)) {
    event_message("system.error", "Could not start display");
    // Leave the control/recovery channel usable even if the display fails.
  }
  startup_stage="Bluetooth";
  // Allow both render workers to finish allocating before optional radio startup.
  // Wait for actual renderer readiness: a fixed delay can hit temporary
  // initialization allocations and incorrectly refuse the radio budget.
  for(int i=0;i<200 && snapshot().frames<2;i++)vTaskDelay(pdMS_TO_TICKS(25));
  ble_remote_boot();
  startup_stage=nullptr;
  event_message("system.ready", "Scout OS ready");
  char *const line=input_buffer;
  char chunk[256];
  size_t used = 0;
  bool overflow = false;
  for (;;) {
    int n =
        scout_usb_read(chunk, sizeof(chunk), ui_capture_busy()?pdMS_TO_TICKS(1):portMAX_DELAY);
    for (int i = 0; i < n; i++) {
      char ch = chunk[i];
      if (ch == '\r')
        continue;
      if (ch == '\n') {
        if (overflow)
          event_message("protocol.error", "Command too long");
        else if (used) {
          line[used] = 0;
          cJSON *r = cJSON_ParseWithLength(line, used);
          if (r && cJSON_IsObject(r))
            dispatch(r);
          else
            event_message("protocol.error", "Invalid JSON command");
          cJSON_Delete(r);
        }
        memset(line, 0, used);
        used = 0;
        overflow = false;
      } else if (used < INPUT_BYTES - 1)
        line[used++] = ch;
      else
        overflow = true;
    }
    // Bulk capture yields between records, without blocking control/audio RPCs.
    if(ui_capture_step())vTaskDelay(1);
  }
}
