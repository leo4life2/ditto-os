#include "probe.h"
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_psram.h"
#include "esp_flash.h"
#include "esp_partition.h"
#include "driver/temperature_sensor.h"
#include "esp_private/esp_clk.h"
#include "esp_sleep.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"

static volatile uint32_t sink;
static RTC_DATA_ATTR uint32_t sleep_marker;
static RTC_DATA_ATTR uint32_t retained_pattern[16];
static RTC_DATA_ATTR int64_t sleep_started_us;
static int64_t wallclock_us(void){struct timeval tv;gettimeofday(&tv,NULL);return(int64_t)tv.tv_sec*1000000+tv.tv_usec;}
void probe_heaps(const char *stage) {
  multi_heap_info_t a,b;
  heap_caps_get_info(&a,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  heap_caps_get_info(&b,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  RESULT("{\"test\":\"heap\",\"stage\":\"%s\",\"internal_total\":%u,\"internal_free\":%u,\"internal_largest\":%u,\"psram_total\":%u,\"psram_free\":%u,\"psram_largest\":%u,\"dma_free\":%u}", stage,(unsigned)(a.total_free_bytes+a.total_allocated_bytes),(unsigned)a.total_free_bytes,(unsigned)a.largest_free_block,(unsigned)(b.total_free_bytes+b.total_allocated_bytes),(unsigned)b.total_free_bytes,(unsigned)b.largest_free_block,(unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));
}
static void integrity(const char *name,uint32_t caps, size_t reserve) {
 size_t n=heap_caps_get_largest_free_block(caps);
 if(n<=reserve+4096) return;
 n=(n-reserve)&~63;
 volatile uint32_t *p=heap_caps_aligned_alloc(64,n,caps);
 if(!p){RESULT("{\"test\":\"memory_integrity\",\"memory\":\"%s\",\"allocation_failed\":true}",name);return;}
 uint32_t errors=0; int64_t start=esp_timer_get_time();
 const uint32_t seeds[]={0xA5A55A5A,0x5A5AA5A5,0x13579BDF};
 for(int pass=0;pass<3;pass++) {
  for(size_t i=0;i<n/4;i++) {p[i]=seeds[pass] ^ ((uint32_t)i*2654435761u);if((i&65535)==0) vTaskDelay(1);}
  for(size_t i=0;i<n/4;i++) {if(p[i]!=(seeds[pass]^((uint32_t)i*2654435761u)))errors++;if((i&65535)==0)vTaskDelay(1);}
 }
 RESULT("{\"test\":\"memory_integrity\",\"memory\":\"%s\",\"bytes\":%u,\"passes\":3,\"errors\":%"PRIu32",\"elapsed_us\":%"PRId64"}",name,(unsigned)n,errors,esp_timer_get_time()-start);
 heap_caps_free((void*)p);
}
static void memory_speed(const char *name,uint32_t caps,size_t n) {
 uint32_t *a=heap_caps_aligned_alloc(64,n,caps),*b=heap_caps_aligned_alloc(64,n,caps);
 if(!a||!b){heap_caps_free(a);heap_caps_free(b);return;}
 for(size_t i=0;i<n/4;i++)a[i]=i*2654435761u;
 const int reps=100;
 int64_t t=esp_timer_get_time();
 int64_t copy_us=0;
 for(int r=0;r<reps;r++){t=esp_timer_get_time();memcpy(b,a,n);asm volatile("" ::: "memory");copy_us+=esp_timer_get_time()-t;if(r%10==9)vTaskDelay(1);}
 uint32_t v=0; t=esp_timer_get_time();
 int64_t read_us=0;
 for(int r=0;r<reps;r++){t=esp_timer_get_time();for(size_t i=0;i<n/4;i++)v+=((volatile uint32_t*)a)[i];read_us+=esp_timer_get_time()-t;if(r%10==9)vTaskDelay(1);}
 sink=v;
 t=esp_timer_get_time();
 int64_t write_us=0;
 for(int r=0;r<reps;r++){t=esp_timer_get_time();for(size_t i=0;i<n/4;i++)((volatile uint32_t*)b)[i]=i+r;write_us+=esp_timer_get_time()-t;if(r%10==9)vTaskDelay(1);}
 RESULT("{\"test\":\"memory_speed\",\"memory\":\"%s\",\"buffer_bytes_each\":%u,\"repetitions\":%d,\"copy_MB_s\":%.3f,\"scalar_read_MB_s\":%.3f,\"scalar_write_MB_s\":%.3f}",name,(unsigned)n,reps,(double)n*reps/copy_us,(double)n*reps/read_us,(double)n*reps/write_us);
 heap_caps_free(a);heap_caps_free(b);vTaskDelay(10);
}
typedef struct {int core;uint32_t result;int64_t us;SemaphoreHandle_t start,done;} cpu_job_t;
static void cpu_worker(void *arg){
 cpu_job_t *j=arg;xSemaphoreTake(j->start,portMAX_DELAY);
 uint32_t x=0x12345678;int64_t t=esp_timer_get_time();
 for(unsigned i=0;i<5000000;i++){x^=x<<13;x^=x>>17;x^=x<<5;}
 j->us=esp_timer_get_time()-t;j->result=x;j->core=xPortGetCoreID();xSemaphoreGive(j->done);vTaskDelete(NULL);
}
static void cpu_test(void){
 cpu_job_t j[2]={0};
 for(int i=0;i<2;i++){j[i].start=xSemaphoreCreateBinary();j[i].done=xSemaphoreCreateBinary();xTaskCreatePinnedToCore(cpu_worker,"cpu_probe",2048,&j[i],2,NULL,i);}
 UBaseType_t old_priority=uxTaskPriorityGet(NULL);vTaskPrioritySet(NULL,3);
 int64_t t=esp_timer_get_time();for(int i=0;i<2;i++)xSemaphoreGive(j[i].start);
 vTaskPrioritySet(NULL,old_priority);
 for(int i=0;i<2;i++)xSemaphoreTake(j[i].done,pdMS_TO_TICKS(5000));
 RESULT("{\"test\":\"cpu_dual_core\",\"iterations_each\":5000000,\"core0_us\":%"PRId64",\"core1_us\":%"PRId64",\"wall_us\":%"PRId64",\"matching_results\":%s}",j[0].us,j[1].us,esp_timer_get_time()-t,j[0].result==j[1].result?"true":"false");
 for(int i=0;i<2;i++){vSemaphoreDelete(j[i].start);vSemaphoreDelete(j[i].done);}
}
static void flash_test(void){
 uint32_t size=0,id=0;TRY(esp_flash_get_physical_size(NULL,&size));TRY(esp_flash_read_id(NULL,&id));
 RESULT("{\"test\":\"flash_identity\",\"physical_bytes\":%"PRIu32",\"jedec_id\":%"PRIu32"}",size,id);
 uint8_t *p=heap_caps_malloc(32768,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);if(!p)return;
 int64_t t=esp_timer_get_time();
 for(int i=0;i<64;i++){esp_err_t e=esp_flash_read(NULL,p,(i*32768)%size,32768);if(e){RESULT("{\"test\":\"flash_read_error\",\"code\":%d}",e);free(p);return;}}
 RESULT("{\"test\":\"flash_read\",\"bytes\":2097152,\"chunk_bytes\":32768,\"MB_s\":%.3f}",2097152.0/(esp_timer_get_time()-t));
 const esp_partition_t *part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x40,"scratch");
 if(part&&part->address==0x1ff0000&&part->size==65536) {
  for(int i=0;i<32768;i++)p[i]=(uint8_t)((i*31)^(i>>8)^0xA5);
  t=esp_timer_get_time();esp_err_t e=esp_partition_erase_range(part,0,65536);int64_t erase=esp_timer_get_time()-t;
  t=esp_timer_get_time();if(!e)e=esp_partition_write(part,0,p,32768);if(!e)e=esp_partition_write(part,32768,p,32768);int64_t write=esp_timer_get_time()-t;
  unsigned errors=0;for(int c=0;c<2&&!e;c++){e=esp_partition_read(part,c*32768,p,32768);for(int i=0;i<32768&&!e;i++)if(p[i]!=(uint8_t)((i*31)^(i>>8)^0xA5))errors++;}
  RESULT("{\"test\":\"flash_scratch\",\"offset\":%u,\"bytes\":65536,\"erase_us\":%"PRId64",\"write_us\":%"PRId64",\"readback_errors\":%u,\"status\":\"%s\"}",(unsigned)part->address,erase,write,errors,esp_err_to_name(e));
 }
 free(p);
}
static void temperature(void){temperature_sensor_handle_t s;temperature_sensor_config_t cfg=TEMPERATURE_SENSOR_CONFIG_DEFAULT(10,80);TRY(temperature_sensor_install(&cfg,&s));TRY(temperature_sensor_enable(s));float c=0;esp_err_t e=temperature_sensor_get_celsius(s,&c);RESULT("{\"test\":\"soc_temperature\",\"celsius\":%.2f,\"status\":\"%s\"}",c,esp_err_to_name(e));temperature_sensor_disable(s);temperature_sensor_uninstall(s);}
void probe_finish_with_grid(void);
void app_main(void){
#ifdef PROBE_FINISH_ONLY
 probe_finish_with_grid();return;
#endif
 bool resumed=esp_reset_reason()==ESP_RST_DEEPSLEEP && sleep_marker==0x53505242;
 int64_t resume_elapsed=resumed?wallclock_us()-sleep_started_us:0;
 usb_serial_jtag_driver_config_t usb={.tx_buffer_size=4096,.rx_buffer_size=1024};
 if(usb_serial_jtag_driver_install(&usb)==ESP_OK)usb_serial_jtag_vfs_use_driver();
 vTaskDelay(pdMS_TO_TICKS(3000));
 if(resumed){
  unsigned errors=0;for(int i=0;i<16;i++)if(retained_pattern[i]!=(0x18273645u^(uint32_t)i))errors++;
  sleep_marker=0;
  RESULT("{\"test\":\"deep_sleep_resume\",\"requested_sleep_us\":1000000,\"sleep_to_app_entry_us\":%"PRId64",\"wakeup_cause\":%d,\"rtc_retained_bytes\":64,\"retention_errors\":%u}",resume_elapsed,(int)esp_sleep_get_wakeup_cause(),errors);
  probe_display();probe_status_screen();
  RESULT("{\"test\":\"complete\",\"phase\":\"after_deep_sleep\",\"heap_integrity\":%s,\"main_stack_free_bytes\":%u}",heap_caps_check_integrity_all(true)?"true":"false",(unsigned)uxTaskGetStackHighWaterMark(NULL));
  while(1)vTaskDelay(pdMS_TO_TICKS(1000));
 }
 esp_chip_info_t chip;esp_chip_info(&chip);
 RESULT("{\"test\":\"begin\",\"probe_version\":\"1.1\",\"idf\":\"%s\",\"cores\":%d,\"revision\":%d,\"cpu_hz\":%d,\"psram_bytes\":%u}",esp_get_idf_version(),chip.cores,chip.revision,esp_clk_cpu_freq(),(unsigned)esp_psram_get_size());
 probe_heaps("boot");temperature();
 integrity("internal",MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT,65536);
 integrity("psram",MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT,131072);
 memory_speed("internal",MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT,32768);
 memory_speed("psram_cache_resident",MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT,4096);
 memory_speed("psram_streaming",MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT,1048576);
 cpu_test();flash_test();probe_sensors();probe_sd();probe_display();probe_audio();probe_radios();probe_audio_stop();temperature();probe_heaps("finished");
 probe_status_screen();
 bool good=heap_caps_check_integrity_all(true);
 RESULT("{\"test\":\"workload_complete\",\"heap_integrity\":%s,\"main_stack_free_bytes\":%u}",good?"true":"false",(unsigned)uxTaskGetStackHighWaterMark(NULL));
 esp_sleep_enable_timer_wakeup(250000);usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));
 int64_t before=esp_timer_get_time();esp_err_t sleep_result=esp_light_sleep_start();int64_t elapsed=esp_timer_get_time()-before;
 vTaskDelay(pdMS_TO_TICKS(1500));
 RESULT("{\"test\":\"light_sleep\",\"requested_us\":250000,\"elapsed_us\":%"PRId64",\"status\":\"%s\",\"wakeup_cause\":%d}",elapsed,esp_err_to_name(sleep_result),(int)esp_sleep_get_wakeup_cause());
 for(int i=0;i<16;i++)retained_pattern[i]=0x18273645u^(uint32_t)i;
 sleep_marker=0x53505242;esp_sleep_enable_timer_wakeup(1000000);
 RESULT("{\"test\":\"deep_sleep_enter\",\"requested_us\":1000000}");usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));
 sleep_started_us=wallclock_us();esp_deep_sleep_start();
 while(1)vTaskDelay(pdMS_TO_TICKS(1000));
}
