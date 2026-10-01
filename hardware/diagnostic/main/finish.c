#include "probe.h"
#include <sys/time.h>
#include <string.h>
#include "esp_system.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"

// macOS may reset the USB bridge during re-enumeration. Retain the measured
// result across that reset; distinguish it from the actual timer wake.
static RTC_NOINIT_ATTR struct {
 uint32_t magic,phase,pattern[16];
 int light_result,deep_reset_reason,deep_wake_cause;
 unsigned retention_errors;
 int64_t light_us,deep_start_us,deep_to_app_us;
} state;
static int64_t rtc_time(void){struct timeval t;gettimeofday(&t,NULL);return(int64_t)t.tv_sec*1000000+t.tv_usec;}
void probe_finish_with_grid(void){
 const uint32_t magic=0x534C5033;
 if(state.magic==magic&&state.phase==3){
  state.deep_to_app_us=rtc_time()-state.deep_start_us;
  state.deep_reset_reason=esp_reset_reason();state.deep_wake_cause=esp_sleep_get_wakeup_cause();
  state.retention_errors=0;for(unsigned i=0;i<16;i++)if(state.pattern[i]!=(0xa5f00d19u^i))state.retention_errors++;
  state.phase=4;
 }
 usb_serial_jtag_driver_config_t usb={.tx_buffer_size=4096,.rx_buffer_size=1024};
 if(usb_serial_jtag_driver_install(&usb)==ESP_OK)usb_serial_jtag_vfs_use_driver();
 vTaskDelay(pdMS_TO_TICKS(4000));
 if(state.magic!=magic){memset(&state,0,sizeof(state));state.magic=magic;}
 if(state.phase==0){
  RESULT("{\"test\":\"sleep_checks_begin\",\"probe_version\":\"1.2\"}");
  usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));
  state.phase=1;esp_sleep_enable_timer_wakeup(250000);int64_t t=esp_timer_get_time();
  state.light_result=esp_light_sleep_start();state.light_us=esp_timer_get_time()-t;state.phase=2;
 }
 if(state.phase==1){state.light_result=ESP_FAIL;state.light_us=-1;state.phase=2;}
 if(state.phase==2){
  for(unsigned i=0;i<16;i++)state.pattern[i]=0xa5f00d19u^i;
  state.phase=3;esp_sleep_enable_timer_wakeup(1000000);state.deep_start_us=rtc_time();esp_deep_sleep_start();
 }
 RESULT("{\"test\":\"light_sleep\",\"requested_us\":250000,\"elapsed_us\":%"PRId64",\"status\":\"%s\"}",state.light_us,esp_err_to_name(state.light_result));
 RESULT("{\"test\":\"deep_sleep_resume\",\"requested_us\":1000000,\"sleep_to_app_entry_us\":%"PRId64",\"reset_reason\":%d,\"wakeup_cause\":%d,\"rtc_retained_bytes\":64,\"retention_errors\":%u,\"timer_wake_verified\":%s}",state.deep_to_app_us,state.deep_reset_reason,state.deep_wake_cause,state.retention_errors,(state.deep_reset_reason==ESP_RST_DEEPSLEEP&&state.deep_wake_cause==ESP_SLEEP_WAKEUP_TIMER)?"true":"false");
 probe_display();probe_calibration_screen(0);
 gpio_config_t key={.pin_bit_mask=1ULL<<0,.mode=GPIO_MODE_INPUT,.pull_up_en=true};gpio_config(&key);
 RESULT("{\"test\":\"complete\",\"phase\":\"calibration_grid\",\"heap_integrity\":%s}",heap_caps_check_integrity_all(true)?"true":"false");
 int page=0,previous=1;
 while(1){int pressed=gpio_get_level(0);if(previous&&!pressed){page^=1;probe_calibration_screen(page);vTaskDelay(pdMS_TO_TICKS(180));}previous=pressed;vTaskDelay(pdMS_TO_TICKS(20));}
}
