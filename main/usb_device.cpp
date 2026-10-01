#include "usb_device.h"
#include "usb_pcm.h"
#include <array>
#include "tusb.h"
#include "esp_private/usb_phy.h"
#include "esp_pm.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "soc/rtc_cntl_reg.h"
#include <cstring>
#include <cmath>
#include <cstdio>
#include <algorithm>

static bool recording=false;
static UsbPcm usb_pcm;
static uint32_t test_until_ms=0,test_sequence=0;
static uint32_t fifo_overwrites=0,packet_bytes=0,packet_empty=0,packet_short=0,packet_long=0;
static uint32_t packet_last_us=0,packet_gap_max_us=0,capture_last_us=0,underrun_age_us=0;
void scout_usb_test_pattern(uint32_t duration_ms){__atomic_store_n(&test_until_ms,uint32_t(esp_timer_get_time()/1000)+duration_ms,__ATOMIC_RELAXED);}
uint32_t scout_usb_metric(unsigned index){
 uint32_t* p[]={&fifo_overwrites,&packet_bytes,&packet_empty,&packet_short,&packet_long,&packet_gap_max_us,&underrun_age_us};
 return index<7?__atomic_load_n(p[index],__ATOMIC_RELAXED):0;
}
static uint32_t audio_drops=0,audio_packets=0,audio_starts=0;
static uint32_t audio_start_us=0;
static int32_t first_audio_us=-1;
static uint8_t mute[2]{};
static int16_t volume[2]{}; // UAC units: 1/256 dB. Only attenuation is exposed.
static float gain=1;
static int64_t boot_at=0;
static char serial_number[20];
static esp_pm_lock_handle_t usb_lock;
static usb_phy_handle_t usb_phy;
static constexpr tusb_desc_device_t descriptor={
 sizeof(tusb_desc_device_t),TUSB_DESC_DEVICE,0x0200,TUSB_CLASS_MISC,
 MISC_SUBCLASS_COMMON,MISC_PROTOCOL_IAD,64,0x303a,0x4001,0x0110,1,2,3,1};
// macOS startup integrity is verified with real PCM, not just first-callback
// timing. Fixed, bus-synchronous packets plus ADC resampling avoid the stale
// blocks observed with variable asynchronous 15/16/17-sample packets.
static constexpr auto make_configuration(){
 std::array<uint8_t,TUD_CONFIG_DESC_LEN+TUD_CDC_DESC_LEN+TUD_AUDIO_MIC_ONE_CH_DESC_LEN> config={
 TUD_CONFIG_DESCRIPTOR(1,4,0,TUD_CONFIG_DESC_LEN+TUD_CDC_DESC_LEN+TUD_AUDIO_MIC_ONE_CH_DESC_LEN,TUSB_DESC_CONFIG_ATT_SELF_POWERED,100),
 TUD_CDC_DESCRIPTOR(0,4,0x81,8,0x02,0x82,64),
 TUD_AUDIO_MIC_ONE_CH_DESCRIPTOR(2,5,2,16,0x83,32)
 };
 for(size_t i=0;i<config.size();i+=config[i]) {
  if(config[i+1]==TUSB_DESC_ENDPOINT && config[i+2]==0x83)
   config[i+3]=TUSB_XFER_ISOCHRONOUS|TUSB_ISO_EP_ATT_SYNCHRONOUS|TUSB_ISO_EP_ATT_DATA;
  if(config[i+1]==TUSB_DESC_CS_INTERFACE && config[i+2]==AUDIO_CS_AC_INTERFACE_CLOCK_SOURCE)
   config[i+4]=AUDIO_CLOCK_SOURCE_ATT_INT_FIX_CLK|AUDIO_CLOCK_SOURCE_ATT_CLK_SYC_SOF;
 }
 return config;
}
static constexpr auto configuration=make_configuration();
extern "C" const uint8_t* tud_descriptor_device_cb(){return (const uint8_t*)&descriptor;}
extern "C" const uint8_t* tud_descriptor_configuration_cb(uint8_t){return configuration.data();}
extern "C" const uint16_t* tud_descriptor_string_cb(uint8_t index,uint16_t){
 static uint16_t result[32];
 const char* strings[]={nullptr,"Scout","Ditto Microphone",serial_number,"Ditto Control","Ditto Microphone"};
 if(index>=sizeof(strings)/sizeof(*strings))return nullptr;
 size_t n=1;if(index==0)result[1]=0x0409;
 else {n=std::min<size_t>(31,strlen(strings[index]));for(size_t i=0;i<n;i++)result[i+1]=strings[index][i];}
 result[0]=(TUSB_DESC_STRING<<8)|(2*n+2);return result;
}
extern "C" void tud_umount_cb(){__atomic_store_n(&recording,false,__ATOMIC_RELAXED);}
extern "C" void tud_suspend_cb(bool){__atomic_store_n(&recording,false,__ATOMIC_RELAXED);}
extern "C" bool tud_audio_set_itf_cb(uint8_t,const tusb_control_request_t* r){
 if((r->wIndex&255)==3){
  if(r->wValue==1){usb_pcm.reset();__atomic_store_n(&audio_start_us,uint32_t(esp_timer_get_time()),__ATOMIC_RELAXED);__atomic_store_n(&first_audio_us,-1,__ATOMIC_RELAXED);__atomic_fetch_add(&audio_starts,1,__ATOMIC_RELAXED);}
  __atomic_store_n(&recording,r->wValue==1,__ATOMIC_RELEASE);
 }
 return true;
}
extern "C" bool tud_audio_set_itf_close_EP_cb(uint8_t,const tusb_control_request_t*){
 __atomic_store_n(&recording,false,__ATOMIC_RELAXED);return true;
}
extern "C" bool tud_audio_tx_done_pre_load_cb(uint8_t,uint8_t,uint8_t,uint8_t){
 int16_t packet[UsbPcm::Packet];
 if(!usb_pcm.pop(packet)){
  __atomic_fetch_add(&audio_drops,1,__ATOMIC_RELAXED);
  __atomic_store_n(&underrun_age_us,uint32_t(esp_timer_get_time())-__atomic_load_n(&capture_last_us,__ATOMIC_RELAXED),__ATOMIC_RELAXED);
 }
 tud_audio_write(packet,sizeof(packet));return true;
}
extern "C" bool tud_audio_tx_done_post_load_cb(uint8_t,uint16_t n,uint8_t,uint8_t,uint8_t){
 if(n)__atomic_fetch_add(&audio_packets,1,__ATOMIC_RELAXED);
 else __atomic_fetch_add(&packet_empty,1,__ATOMIC_RELAXED);
 __atomic_fetch_add(&packet_bytes,n,__ATOMIC_RELAXED);
 if(n==30)__atomic_fetch_add(&packet_short,1,__ATOMIC_RELAXED);
 if(n==34)__atomic_fetch_add(&packet_long,1,__ATOMIC_RELAXED);
 uint32_t now=uint32_t(esp_timer_get_time());
 if(packet_last_us && now-packet_last_us<100000)__atomic_store_n(&packet_gap_max_us,std::max(__atomic_load_n(&packet_gap_max_us,__ATOMIC_RELAXED),now-packet_last_us),__ATOMIC_RELAXED);
 packet_last_us=now;return true;
}
extern "C" bool tud_audio_get_req_entity_cb(uint8_t port,const tusb_control_request_t* r){
 uint8_t entity=r->wIndex>>8, selector=r->wValue>>8,ch=r->wValue&255;
 auto reply=[&](void* p,size_t n){return tud_audio_buffer_and_schedule_control_xfer(port,r,p,n);};
 if(entity==4 && selector==AUDIO_CS_CTRL_SAM_FREQ){
  if(r->bRequest==AUDIO_CS_REQ_CUR){uint32_t rate=16000;return reply(&rate,sizeof(rate));}
  if(r->bRequest==AUDIO_CS_REQ_RANGE){audio_control_range_4_n_t(1) range={1,{{16000,16000,0}}};return reply(&range,sizeof(range));}
 }
 if(entity==4 && selector==AUDIO_CS_CTRL_CLK_VALID && r->bRequest==AUDIO_CS_REQ_CUR){uint8_t v=1;return reply(&v,1);}
 if(entity==1 && selector==AUDIO_TE_CTRL_CONNECTOR && r->bRequest==AUDIO_CS_REQ_CUR){audio_desc_channel_cluster_t c={1,(audio_channel_config_t)0,0};return reply(&c,sizeof(c));}
 if(entity==2 && ch<2){
  if(selector==AUDIO_FU_CTRL_MUTE && r->bRequest==AUDIO_CS_REQ_CUR)return reply(&mute[ch],1);
  if(selector==AUDIO_FU_CTRL_VOLUME){
   if(r->bRequest==AUDIO_CS_REQ_CUR)return reply(&volume[ch],2);
   if(r->bRequest==AUDIO_CS_REQ_RANGE){audio_control_range_2_n_t(1) range={1,{{-60*256,0,256}}};return reply(&range,sizeof(range));}
  }
 }
 return false;
}
extern "C" bool tud_audio_set_req_entity_cb(uint8_t,const tusb_control_request_t* r,uint8_t* data){
 unsigned ch=r->wValue&255,sel=r->wValue>>8;
 if((r->wIndex>>8)!=2 || ch>1 || r->bRequest!=AUDIO_CS_REQ_CUR)return false;
 if(sel==AUDIO_FU_CTRL_MUTE && r->wLength==1){mute[ch]=data[0]!=0;return true;}
 if(sel==AUDIO_FU_CTRL_VOLUME && r->wLength==2){int16_t v;memcpy(&v,data,2);if(v>0 || v<-60*256)return false;volume[ch]=v;gain=powf(10.f,(volume[0]+volume[1])/(256.f*20.f));return true;}
 return false;
}
void scout_usb_bootloader(){boot_at=esp_timer_get_time()+200000;}
extern "C" void tud_cdc_line_coding_cb(uint8_t,const cdc_line_coding_t* coding){if(coding->bit_rate==1200)scout_usb_bootloader();}
static void usb_task(void* starter){
 // Allocate the USB interrupt on the same core that services TinyUSB. The
 // controller/endpoint state must not be advanced concurrently on two cores.
 assert(tusb_init());xTaskNotifyGive((TaskHandle_t)starter);
 bool locked=false;
 for(;;){
  tud_task_ext(2,false);
  bool active=tud_mounted()&&!tud_suspended();
  if(active!=locked){if(active)esp_pm_lock_acquire(usb_lock);else esp_pm_lock_release(usb_lock);locked=active;}
  if(boot_at && esp_timer_get_time()>=boot_at){
   // The S3 ROM downloader uses Serial/JTAG's PHY, not our OTG controller.
   // Explicitly hand the internal PHY back before the software reset.
   tud_disconnect();vTaskDelay(pdMS_TO_TICKS(30));
   usb_del_phy(usb_phy);
   usb_phy_config_t serial_phy={};serial_phy.controller=USB_PHY_CTRL_SERIAL_JTAG;serial_phy.target=USB_PHY_TARGET_INT;
   usb_phy_handle_t serial_handle;ESP_ERROR_CHECK(usb_new_phy(&serial_phy,&serial_handle));
   vTaskDelay(pdMS_TO_TICKS(100));
   REG_WRITE(RTC_CNTL_OPTION1_REG,RTC_CNTL_FORCE_DOWNLOAD_BOOT);esp_restart();
  }
 }
}
void scout_usb_init(){
 uint8_t mac[6];ESP_ERROR_CHECK(esp_read_mac(mac,ESP_MAC_WIFI_STA));
 snprintf(serial_number,sizeof(serial_number),"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
 usb_phy_config_t phy={};phy.controller=USB_PHY_CTRL_OTG;phy.target=USB_PHY_TARGET_INT;phy.otg_mode=USB_OTG_MODE_DEVICE;phy.otg_speed=USB_PHY_SPEED_FULL;
 ESP_ERROR_CHECK(usb_new_phy(&phy,&usb_phy));
 ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_APB_FREQ_MAX,0,"usb_audio",&usb_lock));
 TaskHandle_t starter=xTaskGetCurrentTaskHandle();
 assert(xTaskCreatePinnedToCore(usb_task,"usb_device",4096,starter,10,nullptr,1)==pdPASS);
 assert(ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(2000))!=0);
}
bool scout_usb_connected(){return tud_mounted()&&!tud_suspended();}
bool scout_usb_recording(){return __atomic_load_n(&recording,__ATOMIC_ACQUIRE)&&scout_usb_connected();}
uint32_t scout_usb_audio_drops(){return __atomic_load_n(&audio_drops,__ATOMIC_RELAXED);}
uint32_t scout_usb_audio_packets(){return __atomic_load_n(&audio_packets,__ATOMIC_RELAXED);}
float scout_usb_first_audio_ms(){return __atomic_load_n(&first_audio_us,__ATOMIC_RELAXED)/1000.f;}
uint32_t scout_usb_audio_starts(){return __atomic_load_n(&audio_starts,__ATOMIC_RELAXED);}
void scout_usb_microphone(int16_t* samples,size_t n,bool muted){
 if(!scout_usb_recording())return;
 uint32_t test_until=__atomic_load_n(&test_until_ms,__ATOMIC_RELAXED);
 if(test_until && int32_t(test_until-uint32_t(esp_timer_get_time()/1000))>0)
  for(size_t i=0;i<n;i++)samples[i]=int16_t(int32_t(test_sequence++ & 8191)-4096);
 else if(test_until)__atomic_compare_exchange_n(&test_until_ms,&test_until,0,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED);
 bool silent=muted||mute[0]||mute[1];float g=gain;
 if(silent)memset(samples,0,n*2);else if(g<.999f)for(size_t i=0;i<n;i++)samples[i]=int16_t(samples[i]*g);

 __atomic_store_n(&capture_last_us,uint32_t(esp_timer_get_time()),__ATOMIC_RELAXED);
 size_t wrote=usb_pcm.push(samples,n)?n*2:0;
 if(wrote!=n*2){__atomic_fetch_add(&audio_drops,1,__ATOMIC_RELAXED);__atomic_fetch_add(&fifo_overwrites,1,__ATOMIC_RELAXED);}
 if(wrote && __atomic_load_n(&first_audio_us,__ATOMIC_RELAXED)<0)
  __atomic_store_n(&first_audio_us,int32_t(uint32_t(esp_timer_get_time())-__atomic_load_n(&audio_start_us,__ATOMIC_RELAXED)),__ATOMIC_RELAXED);
}
int scout_usb_write(const char* p,size_t n){
 if(!scout_usb_connected())return n; // Detached device must keep animating.
 size_t written=0;int64_t deadline=esp_timer_get_time()+100000;
 while(written<n && scout_usb_connected() && esp_timer_get_time()<deadline){
  auto count=tud_cdc_write(p+written,n-written);written+=count;tud_cdc_write_flush();if(!count)vTaskDelay(1);
 }
 return written;
}
int scout_usb_read(char* p,size_t n,TickType_t timeout){
 TickType_t start=xTaskGetTickCount();
 do {uint32_t got=tud_cdc_read(p,n);if(got)return got;vTaskDelay(1);}while(timeout==portMAX_DELAY || xTaskGetTickCount()-start<timeout);
 return 0;
}
