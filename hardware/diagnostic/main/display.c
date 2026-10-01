#include "probe.h"
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_sh8601.h"

#define WIDTH 410
#define HEIGHT 502
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t io;
static SemaphoreHandle_t done;
static uint16_t *frame;
static volatile uint32_t te_count;
static volatile int64_t te_first,te_last;
static bool IRAM_ATTR transfer_done(esp_lcd_panel_io_handle_t h,esp_lcd_panel_io_event_data_t *e,void *ctx){BaseType_t wake=pdFALSE;xSemaphoreGiveFromISR(done,&wake);return wake==pdTRUE;}
static void IRAM_ATTR te_isr(void *ctx){int64_t t=esp_timer_get_time();if(!te_count)te_first=t;te_last=t;te_count++;}
static uint16_t swap(uint16_t p){return(p<<8)|(p>>8);}
static esp_err_t draw(int x,int y,int w,int h,const void *p){
 esp_err_t e=esp_lcd_panel_draw_bitmap(panel,x,y,x+w,y+h,p);if(e)return e;
 return xSemaphoreTake(done,pdMS_TO_TICKS(2000))==pdTRUE?ESP_OK:ESP_ERR_TIMEOUT;
}
static int compare(const void *a,const void *b){int64_t x=*(const int64_t*)a,y=*(const int64_t*)b;return(x>y)-(x<y);}
void probe_display_staged(unsigned frames,const char *condition){
 if(!panel||!frame)return;
 uint16_t *stripe=heap_caps_aligned_alloc(64,WIDTH*64*2,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
 if(!stripe){RESULT("{\"test\":\"display_staged\",\"error\":\"allocation_failed\"}");return;}
 if(frames>300)frames=300;
 int64_t times[300],total=0;unsigned ok=0,errors=0;
 for(unsigned n=0;n<frames;n++){
  int64_t t=esp_timer_get_time();
  for(int y=0;y<HEIGHT;y+=64){int h=HEIGHT-y;if(h>64)h=64;memcpy(stripe,frame+y*WIDTH,WIDTH*h*2);esp_err_t e=draw(0,y,WIDTH,h,stripe);if(e){errors++;break;}}
  if(errors)break;
  times[ok++]=esp_timer_get_time()-t;total+=times[ok-1];
 }
 if(ok){qsort(times,ok,sizeof(times[0]),compare);RESULT("{\"test\":\"display_staged\",\"condition\":\"%s\",\"frames\":%u,\"errors\":%u,\"stripe_bytes\":52480,\"transfer_fps\":%.3f,\"mean_us\":%.1f,\"p95_us\":%"PRId64",\"max_us\":%"PRId64"}",condition,ok,errors,ok*1e6/total,(double)total/ok,times[ok*95/100],times[ok-1]);}
 free(stripe);
}
void probe_display_load(unsigned frames,const char *condition){
 if(!panel||!frame)return;
 if(frames>300) {frames=300;}
 int64_t times[300],total=0;unsigned ok=0;
 for(unsigned n=0;n<frames;n++){
  int64_t t=esp_timer_get_time();esp_err_t e=draw(0,0,WIDTH,HEIGHT,frame);int64_t dt=esp_timer_get_time()-t;
  if(e){RESULT("{\"test\":\"display_error\",\"status\":\"%s\"}",esp_err_to_name(e));break;}times[ok++]=dt;total+=dt;
 }
 if(!ok) {return;}
 qsort(times,ok,sizeof(times[0]),compare);
 RESULT("{\"test\":\"display_full_frame\",\"condition\":\"%s\",\"frames\":%u,\"frame_bytes\":411640,\"qspi_hz\":40000000,\"buffer\":\"psram_direct_dma\",\"transfer_fps\":%.3f,\"mean_us\":%.1f,\"p50_us\":%"PRId64",\"p95_us\":%"PRId64",\"max_us\":%"PRId64"}",condition,ok,ok*1e6/total,(double)total/ok,times[ok/2],times[(ok*95)/100],times[ok-1]);
}
 static const sh8601_lcd_init_cmd_t init[]={
 {0x11,(uint8_t[]){0},0,120},{0xc4,(uint8_t[]){0x80},1,0},{0x44,(uint8_t[]){1,0xd1},2,0},
 {0x35,(uint8_t[]){0},1,0},{0x53,(uint8_t[]){0x20},1,10},{0x63,(uint8_t[]){0xff},1,10},
 {0x51,(uint8_t[]){0},1,10},{0x2a,(uint8_t[]){0,0x16,1,0xaf},4,0},{0x2b,(uint8_t[]){0,0,1,0xf5},4,0},
 {0x29,(uint8_t[]){0},0,10},{0x51,(uint8_t[]){64},1,0}};
void probe_display(void){
 done=xSemaphoreCreateBinary();
 spi_bus_config_t b=SH8601_PANEL_BUS_QSPI_CONFIG(11,4,5,6,7,WIDTH*HEIGHT*2+64);TRY(spi_bus_initialize(SPI2_HOST,&b,SPI_DMA_CH_AUTO));
 esp_lcd_panel_io_spi_config_t c=SH8601_PANEL_IO_QSPI_CONFIG(12,transfer_done,NULL);c.flags.psram_dma_direct=true;
 TRY(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&c,&io));

 sh8601_vendor_config_t v={.init_cmds=init,.init_cmds_size=sizeof(init)/sizeof(init[0]),.flags.use_qspi_interface=1};
 esp_lcd_panel_dev_config_t p={.reset_gpio_num=8,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,.bits_per_pixel=16,.vendor_config=&v};
 TRY(esp_lcd_new_panel_sh8601(io,&p,&panel));TRY(esp_lcd_panel_reset(panel));TRY(esp_lcd_panel_init(panel));TRY(esp_lcd_panel_set_gap(panel,22,0));
 gpio_config_t g={.pin_bit_mask=1ULL<<13,.mode=GPIO_MODE_INPUT,.intr_type=GPIO_INTR_POSEDGE};TRY(gpio_config(&g));esp_err_t e=gpio_install_isr_service(0);if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return;TRY(gpio_isr_handler_add(13,te_isr,NULL));
 frame=heap_caps_aligned_alloc(64,(WIDTH*HEIGHT*2+63)&~63,MALLOC_CAP_SPIRAM|MALLOC_CAP_DMA|MALLOC_CAP_8BIT);
 if(!frame){RESULT("{\"test\":\"display\",\"error\":\"no_psram_dma_buffer\"}");return;}
 #ifdef PROBE_FINISH_ONLY
 return;
 #endif
 for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++){
  uint16_t color=(x<WIDTH/3?0x2800:x<WIDTH*2/3?0x0140:0x0005);
  if(x%40==0||y%40==0)color=0x4208;
  frame[y*WIDTH+x]=swap(color);
 }
 TRY(draw(0,0,WIDTH,HEIGHT,frame));vTaskDelay(pdMS_TO_TICKS(2100));
 uint32_t count=te_count;int64_t span=te_last-te_first;
 RESULT("{\"test\":\"display_te\",\"gpio\":13,\"edges\":%"PRIu32",\"observed_hz\":%.3f}",count,count>1&&span>0?(count-1)*1e6/span:0.0);
 probe_display_load(120,"radio_off");
 probe_display_staged(120,"radio_off");
 uint16_t *patch=heap_caps_aligned_alloc(64,64*64*2,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);if(patch){for(int i=0;i<4096;i++)patch[i]=swap(0x0328);int64_t start=esp_timer_get_time();unsigned success=0;for(int i=0;i<200;i++)if(draw(160,220,64,64,patch)==ESP_OK)success++;
 RESULT("{\"test\":\"display_partial\",\"width\":64,\"height\":64,\"transfers\":%u,\"mean_us\":%.2f,\"buffer\":\"internal_dma\"}",success,(esp_timer_get_time()-start)/200.0);free(patch);}
 probe_heaps("display_ready");
}
// Small native bitmap text; all important text stays inside a 50 px safe area.
static const uint8_t letters[][5]={
{126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},{127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},{38,73,73,73,50},{1,1,127,1,1},{63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},{7,8,112,8,7},{97,81,73,69,67}};
static void textline(const char *s,int y,int scale,uint16_t color){int len=strlen(s),x=(WIDTH-len*6*scale)/2;for(int n=0;n<len;n++){if(s[n]>='A'&&s[n]<='Z')for(int col=0;col<5;col++)for(int row=0;row<7;row++)if(letters[s[n]-'A'][col]&(1<<row))for(int dx=0;dx<scale;dx++)for(int dy=0;dy<scale;dy++)frame[(y+row*scale+dy)*WIDTH+x+n*6*scale+col*scale+dx]=swap(color);}}
void probe_status_screen(void){if(!frame||!panel)return;for(int i=0;i<WIDTH*HEIGHT;i++)frame[i]=swap(0x0841);textline("HARDWARE",125,4,0xffff);textline("PROBE COMPLETE",180,3,0x4ff4);textline("SEE MAC REPORT",300,3,0xbdf7);probe_display_staged(1,"final_status_screen");}

static const uint8_t digits[10][5]={{62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30}};
static void pixel(int x,int y,uint16_t color){if(x>=0&&x<WIDTH&&y>=0&&y<HEIGHT)frame[y*WIDTH+x]=swap(color);}
static void label(const char *s,int x,int y,int scale,uint16_t color){
 for(int n=0;s[n];n++){
  const uint8_t *g=NULL;if(s[n]>='A'&&s[n]<='Z')g=letters[s[n]-'A'];else if(s[n]>='0'&&s[n]<='9')g=digits[s[n]-'0'];
  if(g)for(int c=0;c<5;c++)for(int r=0;r<7;r++)if(g[c]&(1<<r))for(int dx=0;dx<scale;dx++)for(int dy=0;dy<scale;dy++)pixel(x+n*6*scale+c*scale+dx,y+r*scale+dy,color);
 }
}
static void box(int x,int y,int w,int h,uint16_t color){for(int i=x;i<x+w;i++){pixel(i,y,color);pixel(i,y+h-1,color);}for(int j=y;j<y+h;j++){pixel(x,j,color);pixel(x+w-1,j,color);}}
void probe_calibration_screen(int page){
 if(!frame||!panel)return;
 for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++){
  uint16_t c=page?(((x/10+y/10)&1)?0x2945:0x1082):0x0841;
  if(x%10==0||y%10==0)c=0x18e3;
  if(x%50==0||y%50==0)c=0x5b6b;
  pixel(x,y,c);
 }
 if(page){
  char n[12];
  for(int y=0;y<HEIGHT;y+=50)for(int x=0;x<WIDTH;x+=50){snprintf(n,sizeof(n),"%d",x);label(n,x+5,y+8,1,0xffff);snprintf(n,sizeof(n),"%d",y);label(n,x+5,y+22,1,0x4fff);}
  for(int y=226;y<280;y++)for(int x=68;x<342;x++)pixel(x,y,0x0841);
  label("X WHITE  Y CYAN",85,234,2,0xffff);label("10 PX CHECKERS",103,257,2,0x4fff);
 }else{
  const int inset[]={0,10,20,30,40,50,60,80};
  const uint16_t color[]={0xffff,0xfba8,0xffe0,0x87e0,0x07ff,0x6cbf,0xfa3f,0xffff};
  char n[12];
  for(int i=0;i<8;i++){
   int d=inset[i];box(d,d,WIDTH-2*d,HEIGHT-2*d,color[i]);
   snprintf(n,sizeof(n),"%d",d);label(n,WIDTH/2+4,d+2,1,color[i]);label(n,d+3,HEIGHT/2+4,1,color[i]);
  }
  for(int y=130;y<373;y++)for(int x=92;x<318;x++)pixel(x,y,0x0841);
  label("SAFE AREA",124,145,3,0xffff);label("INSET IN PIXELS",115,179,2,0xad75);
  for(int i=1;i<8;i++){snprintf(n,sizeof(n),"%d PX",inset[i]);label(n,175,209+(i-1)*19,2,color[i]);}
  label("BOOT FOR GRID",127,352,2,0xad75);
 }
 box(0,0,WIDTH,HEIGHT,0xffff);
 probe_display_staged(1,page?"coordinate_grid":"inset_guides");
 RESULT("{\"test\":\"calibration_grid\",\"page\":%d,\"width\":410,\"height\":502,\"minor_grid_px\":10,\"major_grid_px\":50,\"insets_px\":[0,10,20,30,40,50,60,80],\"safe_area_verified\":false}",page);
}
