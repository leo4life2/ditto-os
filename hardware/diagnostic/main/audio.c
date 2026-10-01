#include "probe.h"
#include <math.h>
#include <string.h>
#include <stdatomic.h>
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev.h"

static esp_codec_dev_handle_t mic,speaker;
static i2s_chan_handle_t tx,rx;
static atomic_bool active;
static SemaphoreHandle_t finished;
static volatile unsigned rx_overflows;
static bool rx_overflow(i2s_chan_handle_t h,i2s_event_data_t *event,void *arg){rx_overflows++;return false;}
static void capture_task(void *arg){
 int16_t samples[512];uint64_t count=0;unsigned errors=0,clips[2]={0},nonzero[2]={0};double sum[2]={0},sq[2]={0};int peak[2]={0};
 rx_overflows=0;
 int64_t start=esp_timer_get_time(),max_read=0;
 while(atomic_load(&active)){
  int64_t t=esp_timer_get_time();int e=esp_codec_dev_read(mic,samples,sizeof(samples));int64_t dt=esp_timer_get_time()-t;if(dt>max_read)max_read=dt;
  if(e){errors++;vTaskDelay(10);continue;}
  for(int i=0;i<512;i++){int k=i&1,v=samples[i],a=v<0?-v:v;sum[k]+=v;sq[k]+=(double)v*v;if(a>peak[k])peak[k]=a;if(a>32760)clips[k]++;if(a)nonzero[k]++;}count+=256;
 }
 int64_t elapsed=esp_timer_get_time()-start;
 RESULT("{\"test\":\"audio_capture\",\"sample_rate_configured\":16000,\"channels\":2,\"bits\":16,\"samples_per_channel\":%llu,\"elapsed_us\":%"PRId64",\"delivered_frames_per_second\":%.2f,\"read_errors\":%u,\"max_read_us\":%"PRId64",\"rms_pcm\":[%.2f,%.2f],\"mean_pcm\":[%.2f,%.2f],\"peak_pcm\":[%d,%d],\"clip_samples\":[%u,%u],\"nonzero_samples\":[%u,%u]}",(unsigned long long)count,elapsed,count*1e6/elapsed,errors,max_read,count?sqrt(sq[0]/count):0,count?sqrt(sq[1]/count):0,count?sum[0]/count:0,count?sum[1]/count:0,peak[0],peak[1],clips[0],clips[1],nonzero[0],nonzero[1]);
 RESULT("{\"test\":\"audio_dma_overflows\",\"count\":%u}",rx_overflows);
 memset(samples,0,sizeof(samples));xSemaphoreGive(finished);vTaskDelete(NULL);
}
void probe_audio(void){
 if(!probe_bus||!probe_found[0x18]||!probe_found[0x40]){RESULT("{\"test\":\"audio\",\"status\":\"codec_address_missing\"}");return;}
 i2s_chan_config_t chan=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);chan.auto_clear=true;
 TRY(i2s_new_channel(&chan,&tx,&rx));
 i2s_std_config_t cfg={.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(16000),.slot_cfg=I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO),.gpio_cfg={.mclk=16,.bclk=41,.ws=45,.dout=40,.din=42}};
 TRY(i2s_channel_init_std_mode(tx,&cfg));TRY(i2s_channel_init_std_mode(rx,&cfg));i2s_event_callbacks_t events={.on_recv_q_ovf=rx_overflow};TRY(i2s_channel_register_event_callback(rx,&events,NULL));TRY(i2s_channel_enable(tx));TRY(i2s_channel_enable(rx));
 audio_codec_i2s_cfg_t data_cfg={.port=0,.rx_handle=rx,.tx_handle=tx};const audio_codec_data_if_t *data=audio_codec_new_i2s_data(&data_cfg);
 audio_codec_i2c_cfg_t out_cfg={.port=1,.addr=ES8311_CODEC_DEFAULT_ADDR,.bus_handle=probe_bus};const audio_codec_ctrl_if_t *out_ctrl=audio_codec_new_i2c_ctrl(&out_cfg);
 es8311_codec_cfg_t out={.ctrl_if=out_ctrl,.gpio_if=audio_codec_new_gpio(),.codec_mode=ESP_CODEC_DEV_WORK_MODE_DAC,.pa_pin=46,.use_mclk=true,.hw_gain={.pa_voltage=5.0,.codec_dac_voltage=3.3}};
 const audio_codec_if_t *out_if=es8311_codec_new(&out);esp_codec_dev_cfg_t od={.dev_type=ESP_CODEC_DEV_TYPE_OUT,.codec_if=out_if,.data_if=data};speaker=esp_codec_dev_new(&od);
 audio_codec_i2c_cfg_t in_cfg={.port=1,.addr=ES7210_CODEC_DEFAULT_ADDR,.bus_handle=probe_bus};const audio_codec_ctrl_if_t *in_ctrl=audio_codec_new_i2c_ctrl(&in_cfg);
 es7210_codec_cfg_t in={.ctrl_if=in_ctrl,.mic_selected=ES7120_SEL_MIC1|ES7120_SEL_MIC2};const audio_codec_if_t *in_if=es7210_codec_new(&in);
 esp_codec_dev_cfg_t md={.dev_type=ESP_CODEC_DEV_TYPE_IN,.codec_if=in_if,.data_if=data};mic=esp_codec_dev_new(&md);
 if(!data||!speaker||!mic){RESULT("{\"test\":\"audio\",\"status\":\"allocation_failed\"}");return;}
 esp_codec_dev_sample_info_t fs={.sample_rate=16000,.channel=2,.bits_per_sample=16};
 int eo=esp_codec_dev_open(speaker,&fs),ei=esp_codec_dev_open(mic,&fs);esp_codec_dev_set_out_vol(speaker,20);esp_codec_dev_set_in_gain(mic,24);
 RESULT("{\"test\":\"audio_open\",\"speaker_status\":%d,\"microphone_status\":%d,\"mic_gain_dB\":24,\"volume_percent\":20}",eo,ei);if(eo||ei)return;
 finished=xSemaphoreCreateBinary();atomic_store(&active,true);xTaskCreatePinnedToCore(capture_task,"mic_probe",4096,NULL,4,NULL,1);
 // Quiet, 0.25-second tone exercises the output path once. No captured PCM is retained.
 int16_t tone[512];int64_t t=esp_timer_get_time();unsigned written=0;int status=0;
 for(int b=0;b<16;b++){for(int i=0;i<256;i++){double env=sin(3.141592653589793*(b*256+i)/4096.0);tone[i*2]=tone[i*2+1]=(int16_t)(2500*env*sin(2*3.141592653589793*660*(b*256+i)/16000.0));}status=esp_codec_dev_write(speaker,tone,sizeof(tone));if(status)break;written+=sizeof(tone);}
 memset(tone,0,sizeof(tone));esp_codec_dev_write(speaker,tone,sizeof(tone));
 RESULT("{\"test\":\"audio_output\",\"pcm_bytes_queued\":%u,\"elapsed_us\":%"PRId64",\"status\":%d,\"acoustic_confirmation\":false}",written,esp_timer_get_time()-t,status);
 vTaskDelay(pdMS_TO_TICKS(1000));probe_display_load(120,"audio_capture");probe_heaps("audio_active");
}
void probe_audio_stop(void){
 if(atomic_load(&active)){atomic_store(&active,false);xSemaphoreTake(finished,pdMS_TO_TICKS(5000));esp_codec_dev_close(mic);esp_codec_dev_close(speaker);gpio_set_level(46,0);}
}
