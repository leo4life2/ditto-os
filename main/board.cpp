#include "ble_remote.h"
#include "usb_device.h"
#include "voice_capture.h"
#include "display_window.h"
#include "scene_layout.h"
#include "touch_input.h"
#include "remote_input.h"
#include "motion_health.h"
#include "activity.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/spi_master.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "mbedtls/base64.h"
#include "scout.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstring>
#include <ctime>
#include <new>
#include <sys/time.h>

i2c_master_bus_handle_t board_i2c;
static i2c_master_dev_handle_t devices[128];
static SemaphoreHandle_t bus_mutex, lcd_mutex, transfer;
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t lcd_io;
static uint16_t *frame, *dma, *ready_frame, *compose_tiles;
static SemaphoreHandle_t present_free, present_ready, te_edge, present_collect;
static TaskHandle_t present_worker;
static esp_timer_handle_t scan_timer;
static volatile int64_t te_last_us=0,te_period_us=16667;
static volatile uint32_t te_edges=0;
static volatile int64_t transfer_done_us=0;
static DisplayWindow dirty[64],ready_dirty[64];
static int dirty_count=0,ready_count=0;
static uint32_t ready_unmerged_strips=0,ready_unmerged_pixels=0;
static bool static_rows_changed=false;
static void presentation_task(void*);
static void IRAM_ATTR te_interrupt(void*) {
  int64_t now=esp_timer_get_time(),delta=now-te_last_us;
  if(delta>10000 && delta<25000)te_period_us=delta;
  te_last_us=now;te_edges=te_edges+1;
  BaseType_t wake=pdFALSE;xSemaphoreGiveFromISR(te_edge,&wake);
  if(wake)portYIELD_FROM_ISR();
}
// DMA staging and raster tiles have independent lifetimes and size needs.
// Twelve-row DMA halves leave more internal RAM for input/audio/networking;
// raster tiles retain native resolution with their own configurable height.
static constexpr int DMA_PIXELS = 2 * SCREEN_W * 12;
// Static clock/status redraws use 16-row bands even when radio-budget raster
// tiles are 12 rows. Keep the PSRAM compositor independent of internal tiles.
static constexpr int COMPOSE_ROWS = std::max(SCENE_TILE_ROWS,18);
static constexpr int COMPOSE_PIXELS = 2 * SCREEN_W * COMPOSE_ROWS;
static esp_codec_dev_handle_t microphone, speaker;
static const audio_codec_if_t *microphone_codec,*speaker_codec;
static const audio_codec_data_if_t *audio_data;
static QueueHandle_t playback;
static bool audio_ready;
static SemaphoreHandle_t audio_rx_mutex,audio_tx_mutex;
static EventGroupHandle_t audio_events;
static constexpr EventBits_t AUDIO_AWAKE=1;
static bool audio_paused=false; // RX/TX locks serialize codec close/open with I/O.
static MotionHealth motion_health;
static bool configure_motion(bool reset) {
  bool ok=true;
  if(reset) {
    ok=board_write(0x6b,0x60,0xb0)==ESP_OK;
    vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t result=0;
    ok=board_read(0x6b,0x4d,&result,1)==ESP_OK && result==0x80 && ok;
  }
  uint8_t id=0,cfg[7]={};
  ok=board_read(0x6b,0,&id,1)==ESP_OK && id==0x05 && ok;
  // +/-8 g, +/-512 deg/s, 112.1 Hz; auto increment, no sample lock/FIFO.
  for(const auto &setting : {std::pair<uint8_t,uint8_t>{8,0},{2,0x40},{3,0x26},{4,0x56},{6,0x77},{8,3}})
    ok=(board_write(0x6b,setting.first,setting.second)==ESP_OK)&&ok;
  ok=board_read(0x6b,2,cfg,sizeof(cfg))==ESP_OK && ok;
  ok=ok && cfg[0]==0x40 && cfg[1]==0x26 && cfg[2]==0x56 && cfg[4]==0x77 && cfg[6]==3;
  motion_health.start(esp_timer_get_time());
  return ok;
}
static void invalidate_bounds(int y, int h);
struct AudioPacket {
  uint16_t count;
  int16_t pcm[320];
};

static esp_err_t read_with_timeout(uint8_t a,uint8_t reg,void *out,size_t len,int timeout_ms,bool *acquired=nullptr) {
  if(acquired)*acquired=false;
  if(xSemaphoreTake(bus_mutex,pdMS_TO_TICKS(timeout_ms))!=pdTRUE)return ESP_ERR_TIMEOUT;
  if(acquired)*acquired=true;
  esp_err_t e = ESP_OK;
  if (!devices[a]) {
    i2c_device_config_t c = {};
    c.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    c.device_address = a;
    c.scl_speed_hz = 400000;
    e = i2c_master_bus_add_device(board_i2c, &c, &devices[a]);
  }
  if (e == ESP_OK)
    e = i2c_master_transmit_receive(devices[a], &reg, 1, (uint8_t *)out, len,
                                    timeout_ms);
  xSemaphoreGive(bus_mutex);
  return e;
}
esp_err_t board_read(uint8_t a,uint8_t reg,void *out,size_t len) {
  return read_with_timeout(a,reg,out,len,100);
}
esp_err_t board_write(uint8_t a, uint8_t reg, uint8_t v) {
  uint8_t unused;
  if (!devices[a])
    board_read(a, reg, &unused, 1);
  xSemaphoreTake(bus_mutex, portMAX_DELAY);
  uint8_t p[] = {reg, v};
  esp_err_t e =
      devices[a] ? i2c_master_transmit(devices[a], p, 2, 100) : ESP_FAIL;
  xSemaphoreGive(bus_mutex);
  return e;
}
static bool IRAM_ATTR done(esp_lcd_panel_io_handle_t,
                           esp_lcd_panel_io_event_data_t *, void *) {
  transfer_done_us=esp_timer_get_time();
  BaseType_t wake = pdFALSE;
  xSemaphoreGiveFromISR(transfer, &wake);
  return wake == pdTRUE;
}
void board_blit(int x, int y, int w, int h, const uint16_t *p,
                int source_stride) {
  if (x < 0 || y < 0 || x + w > SCREEN_W || y + h > SCREEN_H || w <= 0 ||
      h <= 0)
    return;
  xSemaphoreTake(lcd_mutex, portMAX_DELAY);
  invalidate_bounds(y, h);
  if (!source_stride)
    source_stride = w;
  // Preserve the expanded one-pixel border while copying requested pixels
  // directly into the shadow framebuffer and DMA stage in one pass.
  const DisplayWindow window = display_window(x, y, w, h);
  const int strip = display_strip_rows(DMA_PIXELS, window.width);
  assert(strip >= 2 && window.x + window.width <= SCREEN_W &&
         window.y + window.height <= SCREEN_H);
  for (int row = 0; row < window.height; row += strip) {
    int n = std::min(strip, window.height - row);
    for (int j = 0; j < n; j++) {
      int gy = window.y + row + j;
      uint16_t *old = frame + gy * SCREEN_W + window.x;
      uint16_t *out = dma + j * window.width;
      if (gy < y || gy >= y + h) {
        for (int i = 0; i < window.width; i++)
          out[i] = __builtin_bswap16(old[i]);
      } else {
        int pad = x - window.x;
        const uint16_t *src = p + (gy - y) * source_stride;
        if (pad)
          out[0] = __builtin_bswap16(old[0]);
        for (int i = 0; i < w; i++) {
          uint16_t value = src[i];
          old[pad + i] = value;
          out[pad + i] = __builtin_bswap16(value);
        }
        if (pad + w < window.width)
          out[window.width - 1] = __builtin_bswap16(old[window.width - 1]);
      }
    }
    const int x0 = window.x, y0 = window.y + row;
    const int x1 = x0 + window.width, y1 = y0 + n;
    assert(((x0 | y0 | x1 | y1) & 1) == 0);
    esp_err_t e = esp_lcd_panel_draw_bitmap(panel, x0, y0, x1, y1, dma);
    if (e != ESP_OK || xSemaphoreTake(transfer, pdMS_TO_TICKS(500)) != pdTRUE) {
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      state.display_errors++;
      xSemaphoreGive(state_mutex);
      break;
    }
  }
  xSemaphoreGive(lcd_mutex);
}
uint16_t *board_framebuffer() { return ready_frame; }
static bool tile_pending;
static bool capture_shadow;
static bool patch_frame=false,patch_buffers_synced=false,ready_low_power=false;
static int64_t diff_us, copy_us, dma_wait_us, submit_us;
// Every row is 410 RGB565 pixels (820 bytes), and every transfer x/width is
// even. Heap buffers are word-aligned. Alias-safe words avoid the compiler's
// conservative bytewise memcpy expansion for an unknown-alignment uint16_t*.
using PixelPair = uint32_t __attribute__((__may_alias__));
// Bounds belong to their framebuffer, not to a frame number. The spare can
// be two frames old, synchronized by a header/patch update, or left unswapped
// after an empty frame. Pairing ownership keeps black-copy elision exact.
// Horizontal bounds are exclusive pixel-pair indices (0..205), so two
// bytes describe a row exactly and the two images cost only 2 KiB total.
static_assert(SCREEN_W%2==0 && SCREEN_W/2<=255);
struct RowBounds { uint8_t left,end; };
static RowBounds bounds[2][SCREEN_H];
static RowBounds *frame_bounds=bounds[0],*ready_bounds=bounds[1];
static RowBounds preceding_damage[SCREEN_H];
static void invalidate_bounds(int y, int h) {
  for (int row = y; row < y + h; row++) {
    frame_bounds[row]=ready_bounds[row]={0,SCREEN_W/2};
  }
}
static void wait_tile() {
  if (tile_pending && xSemaphoreTake(transfer, pdMS_TO_TICKS(500)) != pdTRUE) {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.display_errors++;
    xSemaphoreGive(state_mutex);
  }
  tile_pending = false;
}
void board_present_begin(bool capture,bool low_power_patch) {
  capture_shadow=capture;dirty_count=0;static_rows_changed=false;
  diff_us=copy_us=dma_wait_us=submit_us=0;
  patch_frame=low_power_patch;
  // The spare image is one presentation behind the current image. Copying
  // current damage plus the preceding presentation's damage catches it up
  // exactly, without rewriting the immutable aura outside both regions.
  for(auto& row:preceding_damage)row={SCREEN_W/2,0};
  for(int i=0;i<ready_count;i++) {
    const auto& a=ready_dirty[i];
    for(int y=a.y;y<a.y+a.height;y++) {
      auto& row=preceding_damage[y];
      row.left=std::min(row.left,uint8_t(a.x/2));row.end=std::max(row.end,uint8_t((a.x+a.width)/2));
    }
  }
  if(patch_frame && !patch_buffers_synced) {
    // Entering partial refresh: seed the spare image from the last complete
    // frame once. Subsequent patches synchronize only their changed rectangles.
    xSemaphoreTake(present_free,portMAX_DELAY);
    memcpy(frame,ready_frame,SCREEN_W*SCREEN_H*2);
    memcpy(frame_bounds,ready_bounds,sizeof(bounds[0]));
    xSemaphoreGive(present_free);patch_buffers_synced=true;
  } else if(!patch_frame)patch_buffers_synced=false;
}
uint16_t* board_tile_buffer(int index) {
  // UI composition must never write the buffers owned by asynchronous DMA.
  return compose_tiles+(index&1)*(COMPOSE_PIXELS/2);
}
bool board_scene_rows_needed(int y,int h,const SceneDamageBounds& damage) {
  if(y<=damage.bottom && y+h>damage.top && damage.left<=damage.right)return true;
  for(int row=y;row<y+h;row++)if(preceding_damage[row].left<preceding_damage[row].end)return true;
  return false;
}
void IRAM_ATTR board_present_tile(int y,int h,uint16_t* pixels,bool continuous,uint16_t*,const SceneDamageBounds* damage) {
  assert((y&1)==0 && (h&1)==0 && h*SCREEN_W<=COMPOSE_PIXELS/2);
  int left=SCREEN_W,right=-1,upper=h,lower=-1;
  RowBounds copy_bounds[COMPOSE_ROWS];
  int64_t start=esp_timer_get_time();
  for(int row=0;row<h;row++) {
    const auto* src=reinterpret_cast<const PixelPair*>(pixels+row*SCREEN_W);
    if(continuous && damage) {
      const bool active=y+row>=damage->top && y+row<=damage->bottom && damage->left<=damage->right;
      const int dl=active?damage->left/2:SCREEN_W/2,de=active?(damage->right/2+1):0;
      const auto prior=preceding_damage[y+row];
      copy_bounds[row]={uint8_t(std::min(dl,int(prior.left))),uint8_t(std::max(de,int(prior.end)))};
      // Conservative nonblack bounds remain safe if a later frame falls
      // back to full comparison (palette, chrome, sleep, or screen change).
      frame_bounds[y+row]={0,SCREEN_W/2};
      if(active){left=std::min(left,dl*2);right=std::max(right,de*2-1);upper=std::min(upper,row);lower=row;}
      continue;
    }
    if(continuous) {
      int lo=0,hi=SCREEN_W/2-1;
      while(lo<=hi && !src[lo])lo++;
      while(hi>=lo && !src[hi])hi--;
      int l=lo*2,r=lo<=hi?hi*2+1:-1;
      const auto old=ready_bounds[y+row],spare=frame_bounds[y+row];
      int union_l=std::min(l,int(old.left)*2),union_r=std::max(r,int(old.end)*2-1);
      copy_bounds[row]={uint8_t(std::min(l/2,int(spare.left))),uint8_t(std::max((r+1)/2,int(spare.end)))};
      frame_bounds[y+row]={uint8_t(l/2),uint8_t((r+1)/2)};
      if(damage) {
        if(y+row<damage->top || y+row>damage->bottom)union_r=-1;
        else {union_l=std::max(union_l,damage->left&~1);union_r=std::min(union_r,damage->right|1);}
      } else {
        // Palette/chrome changes need an exact comparison. Stable frames
        // instead use proven current+previous geometry bounds, avoiding slow
        // reads across unchanged PSRAM background edges.
        const auto* prior=reinterpret_cast<const PixelPair*>(ready_frame+(y+row)*SCREEN_W);
        int dl=union_l/2,dr=union_r<0?-1:union_r/2;
        while(dl<=dr && src[dl]==prior[dl])dl++;
        while(dr>=dl && src[dr]==prior[dr])dr--;
        union_l=dl*2;union_r=dr*2+1;
      }
      if(union_l<=union_r){left=std::min(left,union_l);right=std::max(right,union_r);upper=std::min(upper,row);lower=row;}
      continue;
    }
    // Non-continuous UI rows are copied in full; conservative content bounds
    // stay valid without adding a second scan of their pixels.
    frame_bounds[y+row]={0,SCREEN_W/2};
    const auto* old=reinterpret_cast<const PixelPair*>(ready_frame+(y+row)*SCREEN_W);
    int lo=0,hi=SCREEN_W/2-1;
    while(lo<=hi && src[lo]==old[lo])lo++;
    if(lo>hi)continue;
    while(hi>lo && src[hi]==old[hi])hi--;
    left=std::min(left,lo*2);right=std::max(right,hi*2+1);
    upper=std::min(upper,row);lower=row;
  }
  const int64_t tile_diff_us=esp_timer_get_time()-start;
  start=esp_timer_get_time();
  // Both buffers and the paired-row offset are 32-bit aligned. Copy words
  // explicitly: a generic uint16_t memcpy takes the slow byte-aligned path.
  for(int row=0;row<h;row++) {
    const auto span=continuous?copy_bounds[row]:RowBounds{0,SCREEN_W/2};
    auto* out=reinterpret_cast<PixelPair*>(frame+(y+row)*SCREEN_W);
    const auto* in=reinterpret_cast<const PixelPair*>(pixels+row*SCREEN_W);
    for(int i=span.left;i<span.end;i++)out[i]=in[i];
  }
  const int64_t tile_copy_us=esp_timer_get_time()-start;
  // Workers own disjoint destination rows. Only the small completed-window
  // list and timing totals need serialization; the immutable presenter remains
  // unchanged and is joined before either framebuffer can be reused.
  xSemaphoreTake(present_collect,portMAX_DELAY);
  diff_us+=tile_diff_us;copy_us+=tile_copy_us;
  if(y<SceneLayout::Top)static_rows_changed=true;
  if(right>=left) {
    assert(dirty_count<64);
    auto area=display_window(left,y+upper,right-left+1,lower-upper+1);
    dirty[dirty_count++]=area;
    if(patch_frame)invalidate_bounds(area.y,area.height);
  }
  xSemaphoreGive(present_collect);
}

void board_present_patch(int x,int y,int width,int height,const uint16_t* pixels) {
  assert(patch_frame && !((x|y|width|height)&1) && x>=0 && y>=0 && x+width<=SCREEN_W && y+height<=SCREEN_H);
  bool changed=false;
  for(int row=0;row<height;row++) {
    const auto* src=reinterpret_cast<const PixelPair*>(pixels+row*width);
    const auto* old=reinterpret_cast<const PixelPair*>(ready_frame+(y+row)*SCREEN_W+x);
    auto* dst=reinterpret_cast<PixelPair*>(frame+(y+row)*SCREEN_W+x);
    for(int i=0;i<width/2;i++){changed|=old[i]!=src[i];dst[i]=src[i];}
  }
  if(changed) {assert(dirty_count<64);dirty[dirty_count++]={x,y,width,height};invalidate_bounds(y,height);}
}
void board_present_end() {
  if(dirty_count || capture_shadow) {
    // Only finished frames enter the presenter. The previous upload owns its
    // immutable snapshot until the final DMA completion signal.
    int64_t start=esp_timer_get_time();
    xSemaphoreTake(present_free,portMAX_DELAY);
    dma_wait_us+=esp_timer_get_time()-start;
    start=esp_timer_get_time();
    // Keep immutable UI bands coherent in both buffers only when they change.
    // The old presenter is finished, so its buffer is now safe to prepare.
    if(static_rows_changed) {
      memcpy(ready_frame,frame,SceneLayout::Top*SCREEN_W*2);
      memcpy(ready_bounds,frame_bounds,SceneLayout::Top*sizeof(RowBounds));
    }
    if(patch_frame)for(int i=0;i<dirty_count;i++) {
      const auto& a=dirty[i];
      for(int y=a.y;y<a.y+a.height;y++)
        memcpy(ready_frame+y*SCREEN_W+a.x,frame+y*SCREEN_W+a.x,a.width*2);
    }
    std::swap(ready_frame,frame);
    std::swap(ready_bounds,frame_bounds);
    ready_low_power=patch_frame;
    std::sort(dirty,dirty+dirty_count,[](const DisplayWindow&a,const DisplayWindow&b){return a.y<b.y;});
    ready_unmerged_strips=ready_unmerged_pixels=0;
    for(int i=0;i<dirty_count;i++) {
      const auto& a=dirty[i];const int strip=display_strip_rows(DMA_PIXELS/2,a.width);
      ready_unmerged_strips+=(a.height+strip-1)/strip;
      ready_unmerged_pixels+=a.width*a.height;
    }
    ready_count=display_coalesce(dirty,dirty_count);
    memcpy(ready_dirty,dirty,ready_count*sizeof(DisplayWindow));
    copy_us+=esp_timer_get_time()-start;
    xSemaphoreGive(present_ready);
    if(capture_shadow) {xSemaphoreTake(present_free,portMAX_DELAY);xSemaphoreGive(present_free);}
  }
  xSemaphoreTake(state_mutex,portMAX_DELAY);
  state.diff_ms=diff_us/1000.f;state.copy_ms=copy_us/1000.f;
  state.dma_wait_ms=dma_wait_us/1000.f;state.submit_ms=0;
  xSemaphoreGive(state_mutex);
}
static void wait_until(int64_t target) {
  int64_t remaining=target-esp_timer_get_time();
  // Let the renderer use this core while waiting behind the panel scan. The
  // final absolute-time check keeps timer jitter from ever starting a strip
  // early; completion still has the same next-scan deadline measured below.
  if(scan_timer && remaining>180) {
    ulTaskNotifyTake(pdTRUE,0);
    if(esp_timer_start_once(scan_timer,uint64_t(remaining-40))==ESP_OK)
      ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
  } else if(remaining>1500) {
    vTaskDelay(pdMS_TO_TICKS((remaining-500)/1000));
  }
  remaining=target-esp_timer_get_time();
  if(remaining>0)esp_rom_delay_us(unsigned(remaining));
}
static void presentation_task(void*) {
  esp_timer_create_args_t timer={};
  timer.callback=[](void* task){xTaskNotifyGive(static_cast<TaskHandle_t>(task));};
  timer.arg=xTaskGetCurrentTaskHandle();timer.dispatch_method=ESP_TIMER_TASK;
  timer.name="display_scan";
  // A failed allocation keeps the original bounded wait path available.
  if(esp_timer_create(&timer,&scan_timer)!=ESP_OK)scan_timer=nullptr;
  auto power=power_render_lock();
  for(;;) {
    xSemaphoreTake(present_ready,portMAX_DELAY);
    const bool boost=!ready_low_power;
    if(boost)esp_pm_lock_acquire(power);
    xSemaphoreTake(lcd_mutex,portMAX_DELAY);
    xSemaphoreTake(te_edge,0); // Discard an edge that preceded this complete frame.
    int64_t wait_start=esp_timer_get_time();
    bool synced=xSemaphoreTake(te_edge,pdMS_TO_TICKS(50))==pdTRUE;
    const int64_t origin=te_last_us,period=te_period_us;
    int64_t start=esp_timer_get_time();
    uint32_t missed=0;float min_margin=1000;
    tile_pending=false;int tile=0;uint32_t sent_pixels=0;
    int64_t pending_deadline=0;
    auto complete=[&]() {
      bool pending=tile_pending;wait_tile();
      if(synced && pending) {
        float margin=(pending_deadline-transfer_done_us)/1000.f;
        min_margin=std::min(min_margin,margin);if(margin<0)missed++;
      }
    };
    for(int band=0;band<ready_count;band++) {
      const auto& a=ready_dirty[band];
      const int strip=display_strip_rows(DMA_PIXELS/2,a.width);
      for(int row=0;row<a.height;row+=strip) {
        int n=std::min(strip,a.height-row),y=a.y+row;
        sent_pixels+=n*a.width;
        auto* target=dma+(tile++&1)*(DMA_PIXELS/2);
        for(int j=0;j<n;j++) {
          const auto* src=reinterpret_cast<const PixelPair*>(ready_frame+(y+j)*SCREEN_W+a.x);
          auto* dst=reinterpret_cast<PixelPair*>(target+j*a.width);
          for(int x=0;x<a.width/2;x++){uint32_t p=src[x];dst[x]=((p&0x00ff00ffu)<<8)|((p>>8)&0x00ff00ffu);}
        }
        complete();
        // TE is at line 64. Write behind the panel's current scan and finish
        // before its next pass reaches the same rows. Never race partial render
        // tiles against the beam. Retain the measured safe 40 MHz QSPI clock.
        if(synced)wait_until(origin+(int64_t(y+n)-64)*period/SCREEN_H+250);
        esp_err_t error=esp_lcd_panel_draw_bitmap(panel,a.x,y,a.x+a.width,y+n,target);
        tile_pending=error==ESP_OK;
        if(error!=ESP_OK){xSemaphoreTake(state_mutex,portMAX_DELAY);state.display_errors++;xSemaphoreGive(state_mutex);}
        pending_deadline=origin+(int64_t(y)-64)*period/SCREEN_H+period;
        // Fill the alternate DMA buffer while this strip is on the wire.
        // Completion timestamps come from the ISR, not when the task wakes.
      }
    }
    complete();
    int64_t finish=esp_timer_get_time();
    xSemaphoreGive(lcd_mutex);
    xSemaphoreTake(state_mutex,portMAX_DELAY);
    state.present_strips=tile;state.present_unmerged_strips=ready_unmerged_strips;
    state.present_pixels=sent_pixels;state.present_unmerged_pixels=ready_unmerged_pixels;
    state.presented_frames++;state.te_timeouts+=!synced;state.scan_deadline_misses+=missed;
    state.te_hz=1e6f/period;state.present_ms=(finish-start)/1000.f;
    state.te_wait_ms=(start-wait_start)/1000.f;state.scan_margin_ms=min_margin;
    xSemaphoreGive(state_mutex);
    if(boost)esp_pm_lock_release(power);
    xSemaphoreGive(present_free);
  }
}
void board_clear() {
  memset(frame, 0, SCREEN_W * SCREEN_H * 2);
  board_blit(0, 0, SCREEN_W, SCREEN_H, frame);
  // The panel and both software buffers now contain black. No stale silhouette
  // exists to erase during the first animated frame.
  for(int y=0;y<SCREEN_H;y++){
    frame_bounds[y]=ready_bounds[y]={SCREEN_W/2,0};
  }

}
void board_brightness(int value) {
  uint8_t v = std::clamp(value, 1, 100) * 255 / 100;
  xSemaphoreTake(lcd_mutex, portMAX_DELAY);
  esp_err_t error = esp_lcd_panel_io_tx_param(lcd_io, (0x02 << 24) | (0x51 << 8), &v, 1);
  xSemaphoreGive(lcd_mutex);
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  if (error == ESP_OK) state.effective_brightness = std::clamp(value, 1, 100);
  else state.display_errors++;
  xSemaphoreGive(state_mutex);
}
void board_init() {
  bus_mutex = xSemaphoreCreateMutex();
  lcd_mutex = xSemaphoreCreateMutex();
  transfer = xSemaphoreCreateBinary();
  i2c_master_bus_config_t b = {};
  b.i2c_port = 1;
  b.sda_io_num = GPIO_NUM_15;
  b.scl_io_num = GPIO_NUM_14;
  b.clk_source = I2C_CLK_SRC_DEFAULT;
  b.glitch_ignore_cnt = 7;
  b.flags.enable_internal_pullup = true;
  ESP_ERROR_CHECK(i2c_new_master_bus(&b, &board_i2c));
  gpio_config_t touch = {};
  touch.pin_bit_mask = 1ULL << 9;
  touch.mode = GPIO_MODE_OUTPUT;
  gpio_config(&touch);
  gpio_set_level(GPIO_NUM_9, 0);
  vTaskDelay(pdMS_TO_TICKS(20));
  gpio_set_level(GPIO_NUM_9, 1);
  vTaskDelay(pdMS_TO_TICKS(50));
  // Keep the digitizer sampling while the AMOLED is dimmed, for tap-to-wake.
  board_write(0x38, 0xA5, 0);
  uint8_t adc;
  if (board_read(0x34, 0x30, &adc, 1) == ESP_OK)
    board_write(0x34, 0x30, adc | 0x1d);
  uint8_t rtc[7];
  if (board_read(0x51, 4, rtc, 7) == ESP_OK && !(rtc[0] & 0x80)) {
    auto dec = [](uint8_t v) { return (v >> 4) * 10 + (v & 15); };
    tm t = {};
    t.tm_sec = dec(rtc[0] & 0x7f);
    t.tm_min = dec(rtc[1] & 0x7f);
    t.tm_hour = dec(rtc[2] & 0x3f);
    t.tm_mday = dec(rtc[3] & 0x3f);
    t.tm_mon = dec(rtc[5] & 0x1f) - 1;
    t.tm_year = 100 + dec(rtc[6]);
    if (t.tm_year >= 124 && t.tm_mon >= 0 && t.tm_mon < 12 && t.tm_mday > 0 &&
        t.tm_mday <= 31 && t.tm_hour < 24 && t.tm_min < 60 && t.tm_sec < 60) {
      setenv("TZ", "UTC0", 1);
      tzset();
      timeval tv = {};
      tv.tv_sec = mktime(&t);
      settimeofday(&tv, nullptr);
    }
  }
  bool imu_ok=configure_motion(true);
  state.imu_configured=imu_ok;
  if(!imu_ok)event_message("motion.error","IMU initialization/readback failed; motion disabled");
  spi_bus_config_t spi = {};
  spi.sclk_io_num = 11;
  spi.data0_io_num = 4;
  spi.data1_io_num = 5;
  spi.data2_io_num = 6;
  spi.data3_io_num = 7;
  spi.max_transfer_sz = DMA_PIXELS * 2 + 64;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &spi, SPI_DMA_CH_AUTO));
  esp_lcd_panel_io_spi_config_t c =
      SH8601_PANEL_IO_QSPI_CONFIG(12, done, nullptr);
  c.flags.psram_dma_direct = false;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                           &c, &lcd_io));
  static uint8_t zero[] = {0}, p80[] = {0x80}, te[] = {0, 64}, p20[] = {0x20},
                 pff[] = {0xff}, column[] = {0, 0x16, 1, 0xaf},
                 rows[] = {0, 0, 1, 0xf5}, bright[] = {140};
  static const sh8601_lcd_init_cmd_t cmds[] = {
      {0x11, zero, 0, 120}, {0xc4, p80, 1, 0},    {0x44, te, 2, 0},
      {0x35, zero, 1, 0},   {0x53, p20, 1, 10},   {0x63, pff, 1, 10},
      {0x51, zero, 1, 10},  {0x2a, column, 4, 0}, {0x2b, rows, 4, 0},
      {0x29, zero, 0, 10},  {0x51, bright, 1, 0}};
  sh8601_vendor_config_t v = {};
  v.init_cmds = cmds;
  v.init_cmds_size = sizeof(cmds) / sizeof(cmds[0]);
  v.flags.use_qspi_interface = 1;
  esp_lcd_panel_dev_config_t p = {};
  p.reset_gpio_num = 8;
  p.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  p.bits_per_pixel = 16;
  p.vendor_config = &v;
  ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(lcd_io, &p, &panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, 22, 0));
  frame = (uint16_t *)heap_caps_calloc(SCREEN_W * SCREEN_H, 2,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  dma = (uint16_t *)heap_caps_aligned_alloc(
      64, DMA_PIXELS * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  ready_frame=(uint16_t*)heap_caps_calloc(SCREEN_W*SCREEN_H,2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  compose_tiles=(uint16_t*)heap_caps_malloc(COMPOSE_PIXELS*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  assert(frame && dma && ready_frame && compose_tiles);
  board_clear();
  present_collect=xSemaphoreCreateMutex();assert(present_collect);
  present_free=xSemaphoreCreateBinary();present_ready=xSemaphoreCreateBinary();te_edge=xSemaphoreCreateBinary();
  assert(present_free && present_ready && te_edge);xSemaphoreGive(present_free);
  gpio_config_t sync={};sync.pin_bit_mask=1ULL<<13;sync.mode=GPIO_MODE_INPUT;sync.intr_type=GPIO_INTR_POSEDGE;
  ESP_ERROR_CHECK(gpio_config(&sync));
  esp_err_t isr=gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  assert(isr==ESP_OK || isr==ESP_ERR_INVALID_STATE);
  ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_NUM_13,te_interrupt,nullptr));
  assert(xTaskCreatePinnedToCore(presentation_task,"display_present",3072,nullptr,6,&present_worker,0)==pdPASS);

  gpio_config_t key = {};
  key.pin_bit_mask = 1ULL;
  key.mode = GPIO_MODE_INPUT;
  key.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&key);
}
static bool overflow(i2s_chan_handle_t, i2s_event_data_t *, void *) {
  __atomic_fetch_add(&state.audio_overflows, 1, __ATOMIC_RELAXED);
  return false;
}
static uint32_t capture_generation=1;
static void capture_task(void *) {
  constexpr int N=320; // Proven 20 ms transport budget under render + speaker load.
  int16_t stereo[N*2], mono[N];
  VoiceCapture processor;
  uint32_t generation=0;
  
  while (true) {
    xEventGroupWaitBits(audio_events,AUDIO_AWAKE,pdFALSE,pdTRUE,portMAX_DELAY);
    xSemaphoreTake(audio_rx_mutex,portMAX_DELAY);
    if(audio_paused) {xSemaphoreGive(audio_rx_mutex);continue;}
    uint32_t current_generation=capture_generation;
    int e=esp_codec_dev_read(microphone,stereo,sizeof(stereo));
    xSemaphoreGive(audio_rx_mutex);
    if(e) {
      xSemaphoreTake(state_mutex,portMAX_DELAY);state.audio_rx_errors++;xSemaphoreGive(state_mutex);
      processor.reset();vTaskDelay(pdMS_TO_TICKS(20));continue;
    }
    if(generation!=current_generation){processor.reset();generation=current_generation;}
    int64_t raw_sum=0,right_sum=0,sum=0;int raw_peak=0,peak=0;
    for(int i=0;i<N;i++){
      mono[i]=stereo[i*2];raw_sum+=(int64_t)mono[i]*mono[i];
      right_sum+=(int64_t)stereo[i*2+1]*stereo[i*2+1];raw_peak=std::max(raw_peak,std::abs(int(mono[i])));
    }
    int64_t dsp_start=esp_timer_get_time();
    for(int i=0;i<N;i+=VoiceCapture::BlockSamples)processor.process(mono+i,VoiceCapture::BlockSamples);
    uint32_t dsp_us=uint32_t(esp_timer_get_time()-dsp_start);
    for(int i=0;i<N;i++){sum+=(int64_t)mono[i]*mono[i];peak=std::max(peak,std::abs(int(mono[i])));}
    xSemaphoreTake(state_mutex,portMAX_DELAY);
    state.audio_input_blocks++;
    state.mic_rms=sqrtf(sum/float(N))/32768.f;
    state.mic_raw_rms=sqrtf(raw_sum/float(N))/32768.f;
    state.mic_right_rms=sqrtf(right_sum/float(N))/32768.f;
    state.mic_raw_peak=raw_peak;state.mic_peak=peak;state.mic_settling=processor.settlingNow();
    state.mic_dsp_max_us=std::max(state.mic_dsp_max_us,dsp_us);
    bool muted=state.muted,stream=state.audio_stream&&!muted;
    xSemaphoreGive(state_mutex);
    if(stream)emit_audio_input(reinterpret_cast<const uint8_t*>(mono),sizeof(mono));
    ble_remote_microphone(mono,N,muted);
    scout_usb_microphone(mono,N,muted);
  }
}
static void playback_task(void *) {
  AudioPacket p;
  int16_t stereo[640];
  while (true) {
    xEventGroupWaitBits(audio_events,AUDIO_AWAKE,pdFALSE,pdTRUE,portMAX_DELAY);
    if (xQueueReceive(playback, &p, pdMS_TO_TICKS(50)) != pdTRUE) {
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      state.speaker_rms = 0;
      xSemaphoreGive(state_mutex);
      continue;
    }
    int64_t sum = 0;
    for (int i = 0; i < p.count; i++) {
      stereo[i * 2] = stereo[i * 2 + 1] = p.pcm[i];
      sum += (int64_t)p.pcm[i] * p.pcm[i];
    }
    xSemaphoreTake(audio_tx_mutex,portMAX_DELAY);
    if(audio_paused) {xSemaphoreGive(audio_tx_mutex);continue;}
    int e = esp_codec_dev_write(speaker, stereo, p.count * 4);
    xSemaphoreGive(audio_tx_mutex);
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.audio_output_blocks++;
    state.speaker_rms = p.count ? sqrtf(sum / (float)p.count) / 32768 : 0;
    if (e)
      state.audio_tx_errors++;
    xSemaphoreGive(state_mutex);
  }
}
void board_audio_init() {
  audio_rx_mutex=xSemaphoreCreateMutex();audio_tx_mutex=xSemaphoreCreateMutex();
  audio_events=xEventGroupCreate();
  assert(audio_rx_mutex && audio_tx_mutex && audio_events);
  i2s_chan_handle_t tx, rx;
  i2s_chan_config_t chan =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  // 10 ms DMA blocks keep capture delivery regular; eight descriptors retain
  // 80 ms of scheduling headroom under simultaneous microphone/speaker load.
  chan.dma_frame_num=160;chan.dma_desc_num=8;
  chan.auto_clear = true;
  ESP_ERROR_CHECK(i2s_new_channel(&chan, &tx, &rx));
  i2s_std_config_t cfg = {};
  cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000);
  cfg.slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                    I2S_SLOT_MODE_STEREO);
  cfg.gpio_cfg.mclk = GPIO_NUM_16;
  cfg.gpio_cfg.bclk = GPIO_NUM_41;
  cfg.gpio_cfg.ws = GPIO_NUM_45;
  cfg.gpio_cfg.dout = GPIO_NUM_40;
  cfg.gpio_cfg.din = GPIO_NUM_42;
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &cfg));
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx, &cfg));
  i2s_event_callbacks_t events = {};
  events.on_recv_q_ovf = overflow;
  ESP_ERROR_CHECK(i2s_channel_register_event_callback(rx, &events, nullptr));
  ESP_ERROR_CHECK(i2s_channel_enable(tx));
  ESP_ERROR_CHECK(i2s_channel_enable(rx));
  audio_codec_i2s_cfg_t dc = {};
  dc.port = 0;
  dc.rx_handle = rx;
  dc.tx_handle = tx;
  const auto *data = audio_data = audio_codec_new_i2s_data(&dc);
  audio_codec_i2c_cfg_t oc = {};
  oc.port = 1;
  oc.addr = ES8311_CODEC_DEFAULT_ADDR;
  oc.bus_handle = board_i2c;
  es8311_codec_cfg_t out = {};
  out.ctrl_if = audio_codec_new_i2c_ctrl(&oc);
  out.gpio_if = audio_codec_new_gpio();
  out.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
  out.pa_pin = 46;
  out.use_mclk = true;
  out.hw_gain.pa_voltage = 5;
  out.hw_gain.codec_dac_voltage = 3.3;
  esp_codec_dev_cfg_t od = {};
  od.dev_type = ESP_CODEC_DEV_TYPE_OUT;
  od.codec_if = speaker_codec = es8311_codec_new(&out);
  od.data_if = data;
  speaker = esp_codec_dev_new(&od);
  audio_codec_i2c_cfg_t ic = {};
  ic.port = 1;
  ic.addr = ES7210_CODEC_DEFAULT_ADDR;
  ic.bus_handle = board_i2c;
  es7210_codec_cfg_t in = {};
  in.ctrl_if = audio_codec_new_i2c_ctrl(&ic);
  in.mic_selected = ES7120_SEL_MIC1 | ES7120_SEL_MIC2;
  esp_codec_dev_cfg_t id = {};
  id.dev_type = ESP_CODEC_DEV_TYPE_IN;
  id.codec_if = microphone_codec = es7210_codec_new(&in);
  id.data_if = data;
  microphone = esp_codec_dev_new(&id);
  esp_codec_dev_sample_info_t fs = {};
  fs.sample_rate = 16000;
  fs.channel = 2;
  fs.bits_per_sample = 16;
  audio_ready = speaker && microphone &&
                esp_codec_dev_open(speaker, &fs) == 0 &&
                esp_codec_dev_open(microphone, &fs) == 0;
  if (!audio_ready) {
    event_message("audio.error", "Codec initialization failed");
    return;
  }
  esp_codec_dev_set_out_vol(speaker, snapshot().volume);
  esp_codec_dev_set_in_gain(microphone,VoiceCapture::AnalogGainDb);
  // Bulk PCM waiting for playback belongs in PSRAM. The queue control and
  // codec DMA remain internal; this retains the same 480 ms queue capacity.
  static StaticQueue_t playback_control;
  auto* playback_storage = (uint8_t*)heap_caps_malloc(
      24 * sizeof(AudioPacket), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  assert(playback_storage);
  playback = xQueueCreateStatic(24, sizeof(AudioPacket), playback_storage,
                                 &playback_control);
  assert(playback);
  xEventGroupSetBits(audio_events,AUDIO_AWAKE);
  // Measured >4 KiB unused with PCM streaming; retain >2 KiB margin while
  // returning 2 KiB of scarce internal RAM to device/control tasks.
  xTaskCreatePinnedToCore(capture_task, "mic", 6144, nullptr, 6, nullptr, 1);
  xTaskCreatePinnedToCore(playback_task, "speaker", 4096, nullptr, 4, nullptr,
                          1);
}
void board_audio_sleep(bool sleeping) {
  if(!audio_ready || sleeping==audio_paused)return;
  if(sleeping)xEventGroupClearBits(audio_events,AUDIO_AWAKE);
  xSemaphoreTake(audio_rx_mutex,portMAX_DELAY);
  xSemaphoreTake(audio_tx_mutex,portMAX_DELAY);
  int error=0;
  if(sleeping) {
    // Suspend the already-configured converters, amplifier and shared I2S
    // path. Reopening codec_dev would needlessly reformat disabled channels.
    audio_paused=true;
    xQueueReset(playback);
    error=microphone_codec->enable(microphone_codec,false);
    error|=speaker_codec->enable(speaker_codec,false);
    error|=audio_data->enable(audio_data,ESP_CODEC_DEV_TYPE_IN_OUT,false);
  } else {
    error=audio_data->enable(audio_data,ESP_CODEC_DEV_TYPE_IN_OUT,true);
    error|=speaker_codec->enable(speaker_codec,true);
    error|=microphone_codec->enable(microphone_codec,true);
    error|=esp_codec_dev_set_out_vol(speaker,snapshot().volume);
    error|=esp_codec_dev_set_in_gain(microphone,VoiceCapture::AnalogGainDb);
    if(!error){capture_generation++;audio_paused=false;}
  }
  xSemaphoreTake(state_mutex,portMAX_DELAY);
  state.audio_sleeping=audio_paused;
  state.mic_rms=state.speaker_rms=0;
  if(error)state.audio_power_errors++;
  xSemaphoreGive(state_mutex);
  xSemaphoreGive(audio_tx_mutex);xSemaphoreGive(audio_rx_mutex);
  if(!audio_paused)xEventGroupSetBits(audio_events,AUDIO_AWAKE);
}
void board_volume(int v) {
  if (audio_ready) {
    xSemaphoreTake(audio_tx_mutex,portMAX_DELAY);
    esp_codec_dev_set_out_vol(speaker, v);
    xSemaphoreGive(audio_tx_mutex);
  }
}
bool board_audio_enqueue(const int16_t *data, size_t count) {
  if (!playback || !count || count > 320)
    return false;
  AudioPacket p = {};
  p.count = count;
  memcpy(p.pcm, data, count * 2);
  return xQueueSend(playback, &p, pdMS_TO_TICKS(50)) == pdTRUE;
}
void board_audio_clear() {
  if (playback)
    xQueueReset(playback);
}
void board_tone() {
  int16_t p[320];
  for (int b = 0; b < 10; b++) {
    for (int i = 0; i < 320; i++)
      p[i] = int16_t(2500 * sinf(2 * M_PI * 660 * (b * 320 + i) / 16000) *
                     sinf(M_PI * (b * 320 + i) / 3200));
    board_audio_enqueue(p, 320);
  }
}
static TaskHandle_t touch_worker;
static std::atomic<uint32_t> touch_irq_count{0},touch_irq_us{0};
struct TouchDiagnostics {
  TouchHistory::Queue<> queue;
  RemoteInput::History remote;
  uint32_t bus_errors=0,decode_errors=0,irq_reads=0,poll_reads=0,consumed=0;
  uint32_t idle_bus_deferrals=0;
  bool monitor_wait=false,physical_down=false;
  uint32_t read_max_us=0,irq_to_read_max_us=0,event_age_max_us=0;
  uint32_t irq_latency[8]={},event_latency[8]={};
  uint32_t lock_timeouts=0,transfer_timeouts=0,invalid_state_errors=0;
  struct Fault {int64_t time;uint64_t raw;esp_err_t error;uint32_t irq_age;bool down,physical_down,acquired;int irq_level;};
  Fault faults[16]{};uint32_t fault_count=0;
};
static TouchDiagnostics *touch_diagnostics;
static int latency_bucket(uint32_t us) {
  constexpr uint32_t edge[]={1000,2000,5000,10000,20000,40000,80000};
  int i=0;while(i<7 && us>edge[i])i++;return i;
}
static void IRAM_ATTR touch_interrupt(void*) {
  touch_irq_us.store(uint32_t(esp_timer_get_time()),std::memory_order_relaxed);
  touch_irq_count.fetch_add(1,std::memory_order_relaxed);
  BaseType_t wake=pdFALSE;vTaskNotifyGiveFromISR(touch_worker,&wake);
  if(wake)portYIELD_FROM_ISR();
}
bool board_touch_pop(int64_t cutoff,TouchHistory::Event& event) {
  xSemaphoreTake(state_mutex,portMAX_DELAY);
  bool ok=touch_diagnostics && touch_diagnostics->queue.pop(cutoff,event);
  if(ok) {
    auto& d=*touch_diagnostics;d.consumed++;
    uint32_t age=uint32_t(std::max<int64_t>(0,esp_timer_get_time()-event.time_us));
    d.event_age_max_us=std::max(d.event_age_max_us,age);d.event_latency[latency_bucket(age)]++;
  }
  xSemaphoreGive(state_mutex);return ok;
}
void input_touch(bool down, int x, int y, bool simulated) {
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  const int64_t now = esp_timer_get_time();
  // Real contact always wins. A host test contact has a short lease and never
  // defeats a user touching the panel.
  if (simulated) state.touch_sim_until_us = now + 250000;
  else if (down) state.touch_sim_until_us = 0;
  else if (now < state.touch_sim_until_us) {
    xSemaphoreGive(state_mutex); return;
  }
  bool pressed = down && !state.touch.down;
  if(touch_diagnostics && down!=state.touch.down) {
    auto& h=touch_diagnostics->remote;
    if(down)h.simulated=simulated;
    h.push(now,down?x:state.touch.x,down?y:state.touch.y,down,
           simulated||h.simulated,pressed&&state.sleeping);
  }
  if (pressed) {
    if(state.sleeping)state.touch_wakes++;
    state.touch.press++; state.touch.press_x=x; state.touch.press_y=y;
    if (!simulated) state.touch_contacts++;
    state.sleeping=false; state.revision++;
    state.clip=Avatar::Idle; state.clip_serial++;
  }
  if (down) {
    state.touch.x=x; state.touch.y=y; state.last_activity_us=now;
  }
  state.touch.down=down; state.touch_updated_us=now;
  if(touch_diagnostics)touch_diagnostics->queue.push(state.touch,now);
  xSemaphoreGive(state_mutex);
}
static std::atomic<bool> touch_reset_requested{false};
static TouchRecovery touch_recovery;
void board_touch_reset_request() {touch_reset_requested.store(true,std::memory_order_release);}
static void reset_touch_controller(int64_t now) {
  touch_recovery.resetWindow(now);
  input_touch(false,0,0);
  xSemaphoreTake(state_mutex,portMAX_DELAY);
  state.touch_available=false;state.touch_recoveries++;touch_diagnostics->physical_down=false;
  xSemaphoreGive(state_mutex);
  gpio_set_level(GPIO_NUM_9,0);
  vTaskDelay(pdMS_TO_TICKS(20));
  gpio_set_level(GPIO_NUM_9,1);
  vTaskDelay(pdMS_TO_TICKS(50));
  board_write(0x38,0xA5,0);
  event_message("touch.reset","Digitizer restarted; awaiting valid samples");
}
void board_touch_tick() {
  if(touch_reset_requested.exchange(false,std::memory_order_acq_rel)) {
    reset_touch_controller(esp_timer_get_time());return;
  }
  const int64_t read_start=esp_timer_get_time();
  uint8_t bytes[5] = {}; TouchPoint point;
  esp_err_t last_error=ESP_OK;
  auto read=[&]() {
    memset(bytes,0,sizeof(bytes));
    bool acquired=false;
    esp_err_t error=read_with_timeout(0x38,0x02,bytes,sizeof(bytes),10,&acquired);
    last_error=error;
    bool valid=error==ESP_OK && decode_touch(bytes,point);
    if(!valid) {
      uint64_t raw=0;for(uint8_t b:bytes)raw=(raw<<8)|b;
      int64_t now=esp_timer_get_time();
      xSemaphoreTake(state_mutex,portMAX_DELAY);
      auto& d=*touch_diagnostics;
      if(error!=ESP_OK)d.bus_errors++;else d.decode_errors++;
      if(error==ESP_ERR_TIMEOUT){if(acquired)d.transfer_timeouts++;else d.lock_timeouts++;}
      if(error==ESP_ERR_INVALID_STATE)d.invalid_state_errors++;
      d.faults[d.fault_count++%16]={now,raw,error,uint32_t(now)-touch_irq_us.load(),state.touch.down,d.physical_down,acquired,gpio_get_level(GPIO_NUM_38)};
      xSemaphoreGive(state_mutex);
    }
    return valid;
  };
  bool ok=read();
  if (!ok) {
    uint64_t raw=0; for(uint8_t b:bytes) raw=(raw<<8)|b;
    xSemaphoreTake(state_mutex,portMAX_DELAY);
    state.touch_retries++; state.touch_invalid_report=raw;
    xSemaphoreGive(state_mutex);
    // A register update can straddle a read. Retry once before rejecting the
    // sample; retain diagnostics rather than translating corruption into input.
    vTaskDelay(pdMS_TO_TICKS(1));
    ok=read();
  }
  const int64_t now=esp_timer_get_time();
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  auto& diagnostic=*touch_diagnostics;
  bool recent_irq=touch_irq_count.load()>0 && uint32_t(now)-touch_irq_us.load()<500000;
  // A quiet controller can enter its documented monitor mode. Other slaves
  // (IMU/RTC/codecs) then interrupt its I2C state machine until touch wakes it.
  // Retain every bus error, but don't mistake this idle state for an active
  // contact stall. Real report-ready failures keep bounded reset recovery.
  bool idle_deferral=!ok && last_error==ESP_ERR_INVALID_STATE &&
      !diagnostic.physical_down && !recent_irq && gpio_get_level(GPIO_NUM_38)!=0;
  diagnostic.monitor_wait=idle_deferral;
  if(idle_deferral)diagnostic.idle_bus_deferrals++;
  if(ok)diagnostic.physical_down=point.down;
  if(ok || !idle_deferral)state.touch_available=ok;
  touch_diagnostics->read_max_us=std::max(touch_diagnostics->read_max_us,uint32_t(now-read_start));
  if(ok) state.touch_samples++; else state.touch_errors++;
  bool stale=now-state.touch_updated_us>150000;
  xSemaphoreGive(state_mutex);
  if(ok) input_touch(point.down,point.x,point.y);
  else if(stale) input_touch(false,0,0);
  if(touch_recovery.observe(ok,now,idle_deferral))reset_touch_controller(now);
}
static void touch_task(void*) {
  for(;;) {
    xSemaphoreTake(state_mutex,portMAX_DELAY);
    // Virtual gestures share the application queue, not the digitizer state.
    // Only a real held contact needs the fast hardware watchdog.
    bool sleeping=state.sleeping,down=touch_diagnostics->physical_down,monitor=touch_diagnostics->monitor_wait;
    xSemaphoreGive(state_mutex);
    // The interrupt is primary. A watchdog read keeps releases/recovery safe
    // if an edge is lost; a held contact also supports level-style firmware.
    uint32_t notified=ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(down?10:monitor?500:sleeping?100:40));
    uint32_t age=uint32_t(esp_timer_get_time())-touch_irq_us.load();
    xSemaphoreTake(state_mutex,portMAX_DELAY);
    auto& d=*touch_diagnostics;
    if(notified) {d.irq_reads++;d.irq_to_read_max_us=std::max(d.irq_to_read_max_us,age);d.irq_latency[latency_bucket(age)]++;}
    else d.poll_reads++;
    xSemaphoreGive(state_mutex);
    board_touch_tick();
  }
}
void board_touch_start() {
  void *storage=heap_caps_malloc(sizeof(TouchDiagnostics),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  assert(storage);touch_diagnostics=new(storage) TouchDiagnostics;
  gpio_config_t config={};config.pin_bit_mask=1ULL<<38;config.mode=GPIO_MODE_INPUT;
  config.pull_up_en=GPIO_PULLUP_ENABLE;config.intr_type=GPIO_INTR_NEGEDGE;
  ESP_ERROR_CHECK(gpio_config(&config));
  assert(xTaskCreatePinnedToCore(touch_task,"touch_input",3072,nullptr,5,&touch_worker,1)==pdPASS);
  ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_NUM_38,touch_interrupt,nullptr));
}
cJSON *board_touch_stats() {
  // Compact endpoint avoids full system.status serialization in latency tests.
  xSemaphoreTake(state_mutex,portMAX_DELAY);
  const auto& d=*touch_diagnostics;
  cJSON *o=cJSON_CreateObject();
  auto number=[&](const char* k,double v){cJSON_AddNumberToObject(o,k,v);};
  number("uptime_us",esp_timer_get_time());number("irq_count",touch_irq_count.load());
  number("irq_level",gpio_get_level(GPIO_NUM_38));number("irq_reads",d.irq_reads);number("poll_reads",d.poll_reads);
  number("bus_errors",d.bus_errors);number("decode_errors",d.decode_errors);
  number("idle_bus_deferrals",d.idle_bus_deferrals);number("monitor_wait",d.monitor_wait);
  number("physical_down",d.physical_down);number("lock_timeouts",d.lock_timeouts);
  number("transfer_timeouts",d.transfer_timeouts);number("invalid_state_errors",d.invalid_state_errors);
  number("read_max_us",d.read_max_us);number("irq_to_read_max_us",d.irq_to_read_max_us);
  number("event_to_physics_max_us",d.event_age_max_us);
  number("queued",d.queue.count);number("enqueued",d.queue.enqueued);number("coalesced",d.queue.coalesced);
  number("overflow",d.queue.overflows);number("high_water",d.queue.high_water);number("consumed",d.consumed);
  number("press",state.touch.press);number("seen_press",state.tap_seen_press);number("down",state.touch.down);
  number("stretch",state.slime_stretch);number("tap_count",state.tap_count);number("mode",state.behavior_mode);
  number("samples",state.touch_samples);number("errors",state.touch_errors);number("recoveries",state.touch_recoveries);
  number("task_stack_free",uxTaskGetStackHighWaterMark(touch_worker));
  auto histogram=[&](const char *key,const uint32_t *bins) {
    auto a=cJSON_AddArrayToObject(o,key);for(int i=0;i<8;i++)cJSON_AddItemToArray(a,cJSON_CreateNumber(bins[i]));
  };
  histogram("irq_to_read_bins_us",d.irq_latency);histogram("event_to_physics_bins_us",d.event_latency);
  number("fault_count",d.fault_count);auto faults=cJSON_AddArrayToObject(o,"recent_faults");
  for(uint32_t i=d.fault_count>16?d.fault_count-16:0;i<d.fault_count;i++) {
    const auto& f=d.faults[i%16];auto item=cJSON_CreateObject();
    cJSON_AddNumberToObject(item,"time_us",f.time);cJSON_AddNumberToObject(item,"error",f.error);
    cJSON_AddNumberToObject(item,"irq_age_us",f.irq_age);cJSON_AddBoolToObject(item,"down",f.down);cJSON_AddBoolToObject(item,"physical_down",f.physical_down);
    cJSON_AddBoolToObject(item,"bus_acquired",f.acquired);cJSON_AddNumberToObject(item,"irq_level",f.irq_level);
    char raw[16];snprintf(raw,sizeof(raw),"%010llx",(unsigned long long)f.raw);
    cJSON_AddStringToObject(item,"raw",raw);cJSON_AddItemToArray(faults,item);
  }
  xSemaphoreGive(state_mutex);return o;
}
cJSON *board_remote_input(uint32_t after) {
  RemoteInput::Edge edges[RemoteInput::History::capacity];
  xSemaphoreTake(state_mutex,portMAX_DELAY);
  auto& history=touch_diagnostics->remote;
  unsigned count=history.copy(after,edges);uint32_t seq=history.sequence;
  bool overflow=history.overflow(after),down=state.touch.down,sleep=state.sleeping;
  bool simulated=history.simulated&&down;
  int x=state.touch.x,y=state.touch.y;int64_t now=esp_timer_get_time();
  bool motion_valid=state.motion.valid&&now-state.motion.time_us<150000;
  bool motion_simulated=now<state.motion_sim_until_us;
  auto linear=state.motion.linear;
  xSemaphoreGive(state_mutex);
  auto o=cJSON_CreateObject();
  cJSON_AddNumberToObject(o,"us",now);cJSON_AddNumberToObject(o,"seq",seq);
  cJSON_AddBoolToObject(o,"overflow",overflow);cJSON_AddBoolToObject(o,"down",down);
  cJSON_AddBoolToObject(o,"sleeping",sleep);cJSON_AddBoolToObject(o,"simulated",simulated||motion_simulated);
  cJSON_AddNumberToObject(o,"x",x);cJSON_AddNumberToObject(o,"y",y);
  cJSON_AddBoolToObject(o,"motion_valid",motion_valid);
  auto a=cJSON_AddArrayToObject(o,"linear");
  for(float v:{linear.x,linear.y,linear.z})cJSON_AddItemToArray(a,cJSON_CreateNumber(v));
  a=cJSON_AddArrayToObject(o,"edges");
  for(unsigned i=0;i<count;i++) {
    auto& e=edges[i];auto row=cJSON_CreateArray();
    for(double v:{double(e.seq),double(e.us),double(e.x),double(e.y),double(e.down),double(e.simulated),double(e.woke)})
      cJSON_AddItemToArray(row,cJSON_CreateNumber(v));
    cJSON_AddItemToArray(a,row);
  }
  return o;
}
// Versioned little-endian BLE input: 28-byte header plus four 16-byte edges.
size_t board_remote_packet(uint8_t* out,size_t size) {
  if(size<92 || !touch_diagnostics)return 0;
  memset(out,0,92);auto put=[&](int offset,const auto& v){memcpy(out+offset,&v,sizeof(v));};
  xSemaphoreTake(state_mutex,portMAX_DELAY);
  auto& h=touch_diagnostics->remote;int64_t now=esp_timer_get_time();
  put(0,now);put(8,h.sequence);
  out[12]=(state.touch.down?1:0)|(state.sleeping?2:0)|
      (((h.simulated&&state.touch.down)||now<state.motion_sim_until_us)?4:0)|
      ((state.motion.valid && now-state.motion.time_us<150000)?8:0)|(state.usb_connected?16:0);
  unsigned n=std::min<uint32_t>(4,h.sequence);out[13]=n;out[14]=state.battery_percent<0?255:state.battery_percent;out[15]=1;
  int16_t x=state.touch.x,y=state.touch.y;put(16,x);put(18,y);
  float values[]={state.motion.linear.x,state.motion.linear.y,state.motion.linear.z};
  for(int i=0;i<3;i++){int16_t v=std::clamp(int(values[i]*4096),-32768,32767);put(20+i*2,v);}
  for(unsigned i=0;i<n;i++){
    const auto& e=h.edges[(h.sequence-n+i)%RemoteInput::History::capacity];int offset=28+i*16;
    uint32_t age=std::min<int64_t>(UINT32_MAX,std::max<int64_t>(0,now-e.us));
    put(offset,e.seq);put(offset+4,age);put(offset+8,e.x);put(offset+10,e.y);
    out[offset+12]=(e.down?1:0)|(e.simulated?2:0)|(e.woke?4:0);
  }
  xSemaphoreGive(state_mutex);return 28+n*16;
}
static BlobMotion::Filter live_motion, diagnostic_motion;
static Activity::Motion live_activity, diagnostic_activity;
static std::atomic<bool> motion_stall_requested{false};
void board_motion_stall_request() {motion_stall_requested.store(true,std::memory_order_release);}
void input_motion(BlobMotion::V specific,BlobMotion::V gyro,bool simulated) {
  int64_t now=esp_timer_get_time();
  auto sample=(simulated?diagnostic_motion:live_motion).feed(specific,gyro,now);
  if(!sample.valid)return;
  bool moving=(simulated?diagnostic_activity:live_activity).observe(sample);
  xSemaphoreTake(state_mutex,portMAX_DELAY);
  if(simulated)state.motion_sim_until_us=now+250000;
  if(simulated || now>=state.motion_sim_until_us) {
    state.motion=sample;
    state.motion_active=moving;
    if(moving) {
      state.last_activity_us=now;
      state.last_motion_activity_us=now;
      if(state.sleeping) {
        state.sleeping=false;state.revision++;state.motion_wakes++;
        state.clip=Avatar::Idle;state.clip_serial++;
      }
    }
  }
  xSemaphoreGive(state_mutex);
}
void board_sensor_tick() {
  // Diagnostic fault uses the actual hardware path, not synthetic motion.
  if(motion_stall_requested.exchange(false,std::memory_order_acq_rel))board_write(0x6b,8,0);
  uint8_t r[17]={},stamp[3]={};bool ok=false;
  bool configured;
  xSemaphoreTake(state_mutex,portMAX_DELAY);configured=state.imu_configured;xSemaphoreGive(state_mutex);
  if(configured) {
    // The timestamp brackets each sample batch. Retry a sample that crossed
    // an update rather than combining old/new axes into a spurious impulse.
    for(int attempt=0;attempt<3;attempt++) {
      ok=board_read(0x6b,0x30,r,sizeof(r))==ESP_OK && board_read(0x6b,0x30,stamp,3)==ESP_OK;
      ok=ok && memcmp(r,stamp,3)==0;
      if(ok)break;
      xSemaphoreTake(state_mutex,portMAX_DELAY);state.imu_retries++;xSemaphoreGive(state_mutex);
    }
    const int64_t now=esp_timer_get_time();
    const uint32_t clock=uint32_t(r[0])|(uint32_t(r[1])<<8)|(uint32_t(r[2])<<16);
    bool fresh=ok && motion_health.observe(clock,now);
    xSemaphoreTake(state_mutex,portMAX_DELAY);
    state.imu_available=motion_health.fresh(now);
    if(fresh) {state.imu_samples++;state.imu_timestamp=clock;state.imu_last_sample_us=now;}
    else if(ok)state.imu_stale_reads++;
    else state.imu_errors++;
    xSemaphoreGive(state_mutex);
    if(fresh) {
      auto value=[&](int i){return float(int16_t(uint16_t(r[i])|(uint16_t(r[i+1])<<8)));};
      BlobMotion::V a={value(5)/4096.f,value(7)/4096.f,value(9)/4096.f};
      constexpr float gyro_scale=3.14159265f/(180.f*64.f);
      BlobMotion::V w={value(11)*gyro_scale,value(13)*gyro_scale,value(15)*gyro_scale};
      input_motion(BlobMotion::screenAxes(a),BlobMotion::screenAxes(w));
    }
  }
  if(motion_health.needsReset(esp_timer_get_time())) {
    uint8_t ctrl1=0,ctrl7=0;
    board_read(0x6b,2,&ctrl1,1);board_read(0x6b,8,&ctrl7,1);
    xSemaphoreTake(state_mutex,portMAX_DELAY);
    state.imu_available=false;state.imu_stalls++;state.imu_recoveries++;
    state.imu_fault_ctrl1=ctrl1;state.imu_fault_ctrl7=ctrl7;
    if(esp_timer_get_time()>=state.motion_sim_until_us)state.motion.valid=false;
    xSemaphoreGive(state_mutex);
    bool recovered=configure_motion(true);
    // Keep learned zero-rate bias, discard derivatives across a sensor restart.
    auto bias=live_motion.bias;live_motion={};live_motion.bias=bias;
    live_activity={};
    xSemaphoreTake(state_mutex,portMAX_DELAY);state.imu_configured=recovered;xSemaphoreGive(state_mutex);
    event_message("motion.reset",recovered?"IMU restarted; awaiting advancing samples":"IMU restart failed; will retry");
  }
  static int64_t next_battery_us=0;
  if(esp_timer_get_time()>=next_battery_us) {
    next_battery_us=esp_timer_get_time()+1000000;
    uint8_t pmic[2]={},percent=255,voltage[2]={};
    bool ok=board_read(0x34,0,pmic,2)==ESP_OK;
    bool pok=board_read(0x34,0xa4,&percent,1)==ESP_OK;
    bool vok=board_read(0x34,0x34,voltage,2)==ESP_OK;
    const auto battery=Battery::decode(ok,pmic[0],pmic[1]);
    xSemaphoreTake(state_mutex,portMAX_DELAY);
    if(vok)state.battery_v=(((voltage[0]&31)<<8)|voltage[1])/1000.f;
    state.battery_voltage_valid=vok && battery.present;
    state.battery_percent=ok && pok && battery.present && percent<=100 ? percent:-1;
    state.battery_mode=battery.mode;
    state.battery_external=battery.external;
    state.battery_present=battery.present;
    state.battery_status_valid=ok;
    state.battery_status0=pmic[0];state.battery_status1=pmic[1];
    state.battery_charging=battery.mode==Battery::Mode::Charging;
    xSemaphoreGive(state_mutex);
  }
}
void board_clock_write(int64_t epoch) {
  time_t now = epoch;
  tm t;
  gmtime_r(&now, &t);
  auto bcd = [](int v) -> uint8_t { return ((v / 10) << 4) | (v % 10); };
  uint8_t data[] = {bcd(t.tm_sec),       bcd(t.tm_min),      bcd(t.tm_hour),
                    bcd(t.tm_mday),      (uint8_t)t.tm_wday, bcd(t.tm_mon + 1),
                    bcd(t.tm_year - 100)};
  uint8_t ctrl = 0;
  if (board_read(0x51, 0, &ctrl, 1) != ESP_OK)
    return;
  board_write(0x51, 0, ctrl | 0x20);
  for (int i = 0; i < 7; i++)
    board_write(0x51, 4 + i, data[i]);
  board_write(0x51, 0, ctrl & ~0x20);
}
