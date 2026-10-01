#include "probe.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

i2c_master_bus_handle_t probe_bus;
bool probe_found[128];
static i2c_master_dev_handle_t devs[128];
static esp_err_t device(uint8_t addr){
 if(devs[addr])return ESP_OK;
 i2c_device_config_t cfg={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=addr,.scl_speed_hz=400000};
 return i2c_master_bus_add_device(probe_bus,&cfg,&devs[addr]);
}
esp_err_t probe_read(uint8_t a,uint8_t reg,void *p,size_t n){esp_err_t e=device(a);return e?e:i2c_master_transmit_receive(devs[a],&reg,1,p,n,100);}
esp_err_t probe_write(uint8_t a,uint8_t reg,uint8_t val){esp_err_t e=device(a);uint8_t b[]={reg,val};return e?e:i2c_master_transmit(devs[a],b,2,100);}
static int bcd(uint8_t v){return(v>>4)*10+(v&15);}
static void rtc_probe(void){
 uint8_t a[9]={0},b[9]={0};TRY(probe_read(0x51,0,a,9));int64_t t=esp_timer_get_time();vTaskDelay(pdMS_TO_TICKS(2100));TRY(probe_read(0x51,0,b,9));
 RESULT("{\"test\":\"rtc\",\"oscillator_stop_flag\":%s,\"control1\":%u,\"seconds_before\":%d,\"seconds_after\":%d,\"host_elapsed_us\":%"PRId64"}",(b[4]&0x80)?"true":"false",b[0],bcd(a[4]&127),bcd(b[4]&127),esp_timer_get_time()-t);
 // Date/time is read below using the PCF85063 calendar block (seconds at 0x04).
 uint8_t c[7];TRY(probe_read(0x51,4,c,7));
 RESULT("{\"test\":\"rtc_calendar\",\"date\":\"20%02d-%02d-%02d\",\"time\":\"%02d:%02d:%02d\"}",bcd(c[6]),bcd(c[5]&31),bcd(c[3]&63),bcd(c[2]&63),bcd(c[1]&127),bcd(c[0]&127));
}
static void pmic_probe(void){
 uint8_t s[2],id,adc;TRY(probe_read(0x34,0,s,2));TRY(probe_read(0x34,3,&id,1));TRY(probe_read(0x34,0x30,&adc,1));
 // Enable measurement channels only. Preserve charge, TS and power-rail settings.
 TRY(probe_write(0x34,0x30,adc|0x1d));vTaskDelay(pdMS_TO_TICKS(100));uint8_t d[10];esp_err_t e=probe_read(0x34,0x34,d,10);probe_write(0x34,0x30,adc);if(e)return;
 int raw=((d[8]&63)<<8)|d[9];
 RESULT("{\"test\":\"pmic\",\"chip_id\":%u,\"status1\":%u,\"status2\":%u,\"vbus_good\":%s,\"battery_detected\":%s,\"charge_state\":%u,\"battery_mV_raw\":%u,\"vbus_mV\":%u,\"system_mV\":%u,\"die_temperature_C\":%.2f,\"adc_control_restored\":%u}",id,s[0],s[1],s[0]&32?"true":"false",s[0]&8?"true":"false",s[1]>>5,((d[0]&31)<<8)|d[1],((d[4]&63)<<8)|d[5],((d[6]&63)<<8)|d[7],22.0+(7274-raw)/20.0,adc);
}
static void imu_probe(uint8_t addr){
 uint8_t id[2],saved[8];TRY(probe_read(addr,0,id,2));TRY(probe_read(addr,2,saved,8));
 RESULT("{\"test\":\"imu_identity\",\"address\":%u,\"whoami\":%u,\"revision\":%u}",addr,id[0],id[1]);if(id[0]!=5)return;
 TRY(probe_write(addr,8,0));TRY(probe_write(addr,2,saved[0]|0x40)); // register auto-increment
 TRY(probe_write(addr,3,0x24)); // accelerometer +-8g, 500 Hz nominal
 TRY(probe_write(addr,4,0x54)); // gyro +-512 dps, 448.4 Hz; joint ODR follows gyro
 TRY(probe_write(addr,6,0x11));TRY(probe_write(addr,8,3));vTaskDelay(pdMS_TO_TICKS(150));
 double sum[6]={0},square[6]={0};unsigned n=0,errors=0;uint32_t ts_first=0,ts_last=0;int64_t begin=esp_timer_get_time();
 while(esp_timer_get_time()-begin<2000000){
  uint8_t status=0;if(probe_read(addr,0x2e,&status,1)){errors++;continue;}
  if((status&3)==3){uint8_t r[17];if(probe_read(addr,0x30,r,sizeof(r))){errors++;continue;}
   uint32_t ts=r[0]|r[1]<<8|r[2]<<16;if(!n)ts_first=ts;ts_last=ts;
   for(int k=0;k<6;k++){int16_t v=(int16_t)(r[5+k*2]|r[6+k*2]<<8);double x=v*(k<3?8.0:512.0)/32768.0;sum[k]+=x;square[k]+=x*x;}n++;
  }vTaskDelay(1);
 }
 // Restore ordinary control registers, without issuing command/reset registers.
 probe_write(addr,8,0);probe_write(addr,2,saved[0]);probe_write(addr,3,saved[1]);probe_write(addr,4,saved[2]);probe_write(addr,6,saved[4]);probe_write(addr,8,saved[6]);
 if(!n){RESULT("{\"test\":\"imu_samples\",\"samples\":0,\"errors\":%u}",errors);return;}
 RESULT("{\"test\":\"imu_samples\",\"samples\":%u,\"elapsed_us\":%"PRId64",\"errors\":%u,\"sensor_timestamp_delta\":%u,\"mean_accel_g\":[%.5f,%.5f,%.5f],\"mean_gyro_dps\":[%.5f,%.5f,%.5f],\"stddev_accel_g\":[%.5f,%.5f,%.5f]}",n,esp_timer_get_time()-begin,errors,(unsigned)((ts_last-ts_first)&0xffffff),sum[0]/n,sum[1]/n,sum[2]/n,sum[3]/n,sum[4]/n,sum[5]/n,sqrt(fmax(0,square[0]/n-pow(sum[0]/n,2))),sqrt(fmax(0,square[1]/n-pow(sum[1]/n,2))),sqrt(fmax(0,square[2]/n-pow(sum[2]/n,2))));
}
static void touch_probe(void){
 gpio_config_t g={.pin_bit_mask=1ULL<<9,.mode=GPIO_MODE_OUTPUT};TRY(gpio_config(&g));gpio_set_level(9,0);vTaskDelay(pdMS_TO_TICKS(20));gpio_set_level(9,1);vTaskDelay(pdMS_TO_TICKS(200));
 const uint8_t regs[]={0xa3,0xa6,0xa8,0xaf,0x02};
 for(int i=0;i<sizeof(regs);i++){uint8_t v=0;esp_err_t e=probe_read(0x38,regs[i],&v,1);RESULT("{\"test\":\"touch_register\",\"register\":%u,\"value\":%u,\"status\":\"%s\"}",regs[i],v,esp_err_to_name(e));}
 uint8_t r[14];unsigned success=0;int64_t t=esp_timer_get_time();for(int i=0;i<500;i++)if(probe_read(0x38,0x02,r,sizeof(r))==ESP_OK)success++;
 RESULT("{\"test\":\"touch_poll\",\"attempts\":500,\"success\":%u,\"mean_transaction_us\":%.1f,\"contacts_at_end\":%u}",success,(esp_timer_get_time()-t)/500.0,r[0]&15);
}
void probe_sensors(void){
 i2c_master_bus_config_t cfg={.i2c_port=1,.sda_io_num=15,.scl_io_num=14,.clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};TRY(i2c_new_master_bus(&cfg,&probe_bus));
 gpio_config_t reset={.pin_bit_mask=1ULL<<9,.mode=GPIO_MODE_OUTPUT};TRY(gpio_config(&reset));gpio_set_level(9,0);vTaskDelay(pdMS_TO_TICKS(20));gpio_set_level(9,1);vTaskDelay(pdMS_TO_TICKS(200));
 for(int a=8;a<0x78;a++){esp_err_t e=i2c_master_probe(probe_bus,a,20);if(e==ESP_OK){probe_found[a]=true;RESULT("{\"test\":\"i2c_found\",\"address\":%d}",a);}}
 if(probe_found[0x34]) {pmic_probe();}
 if(probe_found[0x51]) {rtc_probe();}
 if(probe_found[0x6b]) {imu_probe(0x6b);} else if(probe_found[0x6a]) {imu_probe(0x6a);}
 if(probe_found[0x38]) {touch_probe();}
}
void probe_sd(void){
 esp_vfs_fat_sdmmc_mount_config_t m={.format_if_mount_failed=false,.max_files=2,.allocation_unit_size=16384};
 sdmmc_host_t h=SDMMC_HOST_DEFAULT();h.max_freq_khz=20000;
 sdmmc_slot_config_t s=SDMMC_SLOT_CONFIG_DEFAULT();s.width=1;s.clk=2;s.cmd=1;s.d0=3;s.d1=-1;s.d2=-1;s.d3=-1;s.d4=-1;s.d5=-1;s.d6=-1;s.d7=-1;
 sdmmc_card_t *card=NULL;esp_err_t e=esp_vfs_fat_sdmmc_mount("/sd",&h,&s,&m,&card);
 RESULT("{\"test\":\"sd_mount\",\"status\":\"%s\",\"formatted\":false}",esp_err_to_name(e));if(e)return;
 uint8_t *p=heap_caps_malloc(32768,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);if(!p){esp_vfs_fat_sdcard_unmount("/sd",card);return;}
 int64_t t=esp_timer_get_time();unsigned chunks=0;for(int i=0;i<128;i++){e=sdmmc_read_sectors(card,p,i*64,64);if(e)break;chunks++;}
 RESULT("{\"test\":\"sd_read\",\"capacity_bytes\":%llu,\"bus_width\":1,\"clock_kHz\":%d,\"bytes\":%u,\"MB_s\":%.3f,\"status\":\"%s\"}",(unsigned long long)card->csd.capacity*card->csd.sector_size,(int)card->max_freq_khz,chunks*32768,chunks*32768.0/(esp_timer_get_time()-t),esp_err_to_name(e));free(p);esp_vfs_fat_sdcard_unmount("/sd",card);
}
