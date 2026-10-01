#include "fonts.h"
#include "scene_layout.h"
#include "freertos/FreeRTOS.h"
#include "scout.h"
#include "usb_device.h"
#define A3D_VIEWER_NO_STL
#define A3D_VIEWER_NO_STDIO
#include "backends/freertos/a3d_freertos_executor.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"
#include "viewer/a3d_viewer.h"
#include "blob_render.h"
#include "ditto_lighting.h"
#include "ditto_listening_glow.h"
#include "ditto_damage.h"
#include "ditto_autoplay.h"
#include "esp_random.h"
#include "ditto_sleep_marks.h"
#include "brightness_transition.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

extern const uint8_t avatar_start[] asm("_binary_avatar_a3d_start");
extern const uint8_t avatar_end[] asm("_binary_avatar_a3d_end");
// Native-resolution live geometry and typography share one composition.
static constexpr uint16_t INK = 0xff9c, GREY = 0x9cf3, GOLD = 0xfe28,
                          CYAN = 0x659f, CORAL = 0xfb4c;
// Each renderer worker owns its tile through composition. Only submission to
// the display is serialized; overlays can run alongside the other worker.
static thread_local uint16_t *canvas;
static thread_local int top = 0, rows = SCREEN_H;
static thread_local int canvas_left=0,canvas_stride=SCREEN_W;
static int ui_alpha = 255;
static float display_value = 0;
static time_t present_epoch;
static int64_t present_now_us;
static uint16_t blend(uint16_t bg, uint16_t fg, int a) {
  int r = (((fg >> 11) & 31) * a + ((bg >> 11) & 31) * (255 - a)) / 255,
      g = (((fg >> 5) & 63) * a + ((bg >> 5) & 63) * (255 - a)) / 255,
      b = ((fg & 31) * a + (bg & 31) * (255 - a)) / 255;
  return (r << 11) | (g << 5) | b;
}
// The measured corner inset applies to broad content. These narrow centered
// status marks can use the flat top/bottom edges, as requested in review.
static constexpr int STATUS_EDGE_LEFT=180,STATUS_EDGE_RIGHT=230;
static constexpr int STATUS_EDGE_TOP=32,STATUS_EDGE_BOTTOM=490;
static void pixel(int x, int y, uint16_t c, int a = 255, bool edge_status=false) {
  a = a * ui_alpha / 255;
  if (a == 0)
    return;
  if (x < (edge_status?STATUS_EDGE_LEFT:SAFE) || x >= (edge_status?STATUS_EDGE_RIGHT:SCREEN_W-SAFE) ||
      y < (edge_status?STATUS_EDGE_TOP:SAFE) || y >= (edge_status?STATUS_EDGE_BOTTOM:SCREEN_H-SAFE) ||
      y < top || y >= top + rows || x<canvas_left || x>=canvas_left+canvas_stride)
    return;
  int i = (y - top) * canvas_stride + x-canvas_left;
  canvas[i] = a == 255 ? c : blend(canvas[i], c, a);
}
static int width(const Font &f, const char *s) {
  int w = 0;
  for (; *s; s++)
    w += f.glyphs[(unsigned char)*s >= 32 && (unsigned char)*s < 127 ? *s - 32
                                                                     : '?' - 32]
             .advance;
  return w;
}
static void text(const Font &f, const char *s, int x, int y, uint16_t c) {
  for (; *s; s++) {
    unsigned char ch = *s;
    if (ch < 32 || ch > 126)
      ch = '?';
    const Glyph &g = f.glyphs[ch - 32];
    const int gx = x + g.x, gy = y + g.y;
    const int first_row = std::max({0, top - gy, SAFE - gy});
    const int last_row =
        std::min({int(g.h), top + rows - gy, SCREEN_H - SAFE - gy});
    const int first_col = std::max({0,SAFE-gx,canvas_left-gx});
    const int last_col = std::min({int(g.w),SCREEN_W-SAFE-gx,canvas_left+canvas_stride-gx});
    for (int yy = first_row; yy < last_row; yy++) {
      const uint8_t *mask = f.data + g.offset + yy * g.w;
      if (first_col >= last_col)
        continue;
      uint16_t *dest = canvas + (gy+yy-top)*canvas_stride+gx+first_col-canvas_left;
      for (int xx = first_col; xx < last_col; xx++, dest++) {
        int a = mask[xx];
        if (!a)
          continue;
        if (ui_alpha < 255)
          a = a * ui_alpha / 255;
        *dest = a == 255 ? c : blend(*dest, c, a);
      }
    }
    x += g.advance;
  }
}
static void center(const Font &f, const char *s, int y, uint16_t c) {
  text(f, s, (SCREEN_W - width(f, s)) / 2, y, c);
}
static void wrapped(const Font &f, const char *s, int y, uint16_t c,
                    int maxlines = 2) {
  char line[128];
  int used = 0, lines = 0;
  for (size_t i = 0;; i++) {
    char ch = s[i];
    if (used >= int(sizeof(line) - 2))
      ch = 0;
    line[used] = ch;
    line[used + 1] = 0;
    if (ch && width(f, line) > SCREEN_W - 2 * SAFE) {
      line[used] = 0;
      int split = used;
      while (split > 0 && line[split] != ' ')
        split--;
      if (split > 0) {
        i -= used - split;
        line[split] = 0;
      } else
        i--;
      center(f, line, y, c);
      if (++lines >= maxlines)
        return;
      y += f.height;
      used = 0;
    } else if (!ch) {
      center(f, line, y, c);
      return;
    } else
      used++;
  }
}
// Scan results have one line each. Preserve both ends and explicitly mark
// truncation; the selected network is shown in full on the next surface.
static void fit_line(const Font& f,const char* s,char* out,size_t capacity) {
  snprintf(out,capacity,"%s",s);
  if(width(f,out)<=SCREEN_W-2*SAFE)return;
  const int len=strlen(s);int left=(len+1)/2,right=len-left;
  while(left+right>0) {
    snprintf(out,capacity,"%.*s...%s",left,s,s+len-right);
    if(width(f,out)<=SCREEN_W-2*SAFE)return;
    if(left>right)--left;else --right;
  }
  snprintf(out,capacity,"...");
}
// Small antialiased vector marks, clipped to the current composition tile.
// The two status marks straddle the scene; sleep redraws only small patches.
static float status_coverage(float radius,float distance2) {
  // Only the one-pixel antialias fringe needs a square root. This is exactly
  // the old clamped distance for all solid/empty pixels, not an approximation.
  const float inside=radius-.5f,outside=radius+.5f;
  if(inside>=0 && distance2<=inside*inside)return 1;
  if(distance2>=outside*outside)return 0;
  return radius+.5f-sqrtf(distance2);
}
static void status_round(float x,float y,float w,float h,float radius,uint16_t color) {
  const int first=std::max(top,int(floorf(y))),last=std::min(top+rows,int(ceilf(y+h)));
  for(int yy=first;yy<last;yy++) {
    float dy=std::max(0.f,std::abs(yy+.5f-y-h*.5f)-(h*.5f-radius));
    const int left=int(floorf(x)),right=int(ceilf(x+w));
    int solid0=left,solid1=left;
    if(ui_alpha==255 && x==left && y==floorf(y) && w==floorf(w) && h==floorf(h)) {
      solid0=dy==0?left:std::min(right,int(ceilf(x+radius)));
      solid1=dy==0?right:std::max(solid0,int(floorf(x+w-radius)));
      const int a=std::max({solid0,STATUS_EDGE_LEFT,canvas_left});
      const int b=std::min({solid1,STATUS_EDGE_RIGHT,canvas_left+canvas_stride});
      if(a<b && yy>=STATUS_EDGE_TOP && yy<STATUS_EDGE_BOTTOM)
        std::fill(canvas+(yy-top)*canvas_stride+a-canvas_left,
                  canvas+(yy-top)*canvas_stride+b-canvas_left,color);
    }
    for(int xx=left;xx<right;xx++) {
      if(xx==solid0 && solid1>solid0){xx=solid1-1;continue;}
      float dx=std::max(0.f,std::abs(xx+.5f-x-w*.5f)-(w*.5f-radius));
      pixel(xx,yy,color,int(255*status_coverage(radius,dx*dx+dy*dy)),true);
    }
  }
}
static void status_stroke(float ax,float ay,float bx,float by,float weight,uint16_t color) {
  const float vx=bx-ax,vy=by-ay,len=vx*vx+vy*vy,r=weight*.5f;
  int y0=std::max(top,int(floorf(std::min(ay,by)-r-1))),y1=std::min(top+rows,int(ceilf(std::max(ay,by)+r+1)));
  for(int y=y0;y<y1;y++)for(int x=int(floorf(std::min(ax,bx)-r-1));x<int(ceilf(std::max(ax,bx)+r+1));x++) {
    float t=len?std::clamp(((x+.5f-ax)*vx+(y+.5f-ay)*vy)/len,0.f,1.f):0;
    float dx=x+.5f-ax-t*vx,dy=y+.5f-ay-t*vy;
    pixel(x,y,color,int(255*status_coverage(r,dx*dx+dy*dy)),true);
  }
}
static constexpr int HOME_HEADER_BOTTOM=176;
static constexpr int HOME_SLEEP_MARK_OFFSET=SceneLayout::HomeOffset+30;
static int64_t home_charge_tick(int64_t now_us) {return now_us/83333;}
static void home_connection(const State& s) {
  if(top>=60 || top+rows<=32)return;
  const bool connected=s.usb_connected||s.bluetooth_connected;
  const uint16_t accent=connected?CYAN:GREY;
  status_round(190,32,30,26,13,connected?0x10e4:0x18c3);
  if(s.bluetooth_connected&&!s.usb_connected) {
    status_stroke(205,37,205,53,1.8f,accent);
    status_stroke(205,37,211,42,1.8f,accent);
    status_stroke(211,42,199,50,1.8f,accent);
    status_stroke(199,40,211,48,1.8f,accent);
    status_stroke(211,48,205,53,1.8f,accent);
  } else {
    // A USB plug silhouette: two contacts, rounded housing and short cable.
    status_round(199,40,12,9,3,accent);
    status_stroke(202,37,202,40,1.8f,accent);
    status_stroke(208,37,208,40,1.8f,accent);
    status_stroke(205,48,205,53,2.f,accent);
    if(!connected) {
      status_stroke(197,53,213,37,4.5f,0x18c3);
      status_stroke(197,53,213,37,1.7f,accent);
    }
  }

}
static constexpr int HOME_BATTERY_TOP=468,HOME_BATTERY_BOTTOM=490;
static bool home_battery_changed(const State& a,const State& b) {
  return a.battery_percent!=b.battery_percent || a.battery_mode!=b.battery_mode ||
         a.battery_external!=b.battery_external;
}
static void home_battery(const State& s) {
  if(top>=HOME_BATTERY_BOTTOM || top+rows<=HOME_BATTERY_TOP)return;
  constexpr uint16_t WHITE=0xffff,TRACK=0x2125,GREEN=0x4e8f,MINT=0x9fb6,DARK=0x1082;
  const bool charging=s.battery_mode==Battery::Mode::Charging;
  const bool show_bolt=charging || (s.battery_mode==Battery::Mode::Full && s.battery_external);
  const bool paused=s.battery_mode==Battery::Mode::Paused;
  const bool unknown=s.battery_mode==Battery::Mode::Unknown || s.battery_mode==Battery::Mode::Absent;
  const int pct=std::clamp(s.battery_percent,0,100);
  const bool known=!unknown && s.battery_percent>=0;
  const uint16_t fill=paused?GREY:charging?GREEN:pct<=20?CORAL:GREEN;
  // Fixed optical center including the terminal. No outer black keyline or
  // backing: this glyph lives below the settled body.
  constexpr int left=183,inner_width=38;
  status_round(181,468,42,22,6,WHITE);
  status_round(left,470,inner_width,18,4,TRACK);
  status_round(225,475,3,8,1.5f,WHITE);
  const float fill_end=left+inner_width*pct/100.f;
  const float phase=float(home_charge_tick(present_now_us)%36)/36.f;
  if(known && pct==100 && !charging) {
    status_round(left,470,inner_width,18,4,fill);
  } else if(known && pct>0) {
    for(int y=std::max(top,470);y<std::min(top+rows,488);y++)for(int x=left;x<left+inner_width;x++) {
      float dx=std::max(0.f,std::abs(x+.5f-left-inner_width*.5f)-(inner_width*.5f-4));
      float dy=std::max(0.f,std::abs(y+.5f-479)-5.f);
      float coverage=std::min(status_coverage(4,dx*dx+dy*dy),std::clamp(fill_end-x,0.f,1.f));
      uint16_t color=fill;
      if(charging) {
        float glint=std::max(0.f,1.f-std::abs((x+.5f-left)/inner_width-(-.3f+1.6f*phase))/.28f);
        color=blend(fill,MINT,int(150*glint*glint*(3-2*glint)));
      }
      pixel(x,y,color,int(255*coverage),true);
    }
  }
  if(show_bolt) {
    // 4x antialiased filled lightning, baked from six contour points.
    static constexpr uint8_t bolt[14][11]={
      {0,0,0,0,0,16,80,0,0,0,0},
      {0,0,0,0,0,175,64,0,0,0,0},
      {0,0,0,0,80,255,64,0,0,0,0},
      {0,0,0,32,239,255,64,0,0,0,0},
      {0,0,0,191,255,255,64,0,0,0,0},
      {0,0,112,255,255,255,159,128,128,112,0},
      {0,48,255,255,255,255,255,255,255,80,0},
      {0,207,255,255,255,255,255,255,143,0,0},
      {0,0,0,0,175,255,255,207,0,0,0},
      {0,0,0,0,191,255,239,16,0,0,0},
      {0,0,0,0,255,255,64,0,0,0,0},
      {0,0,0,0,255,128,0,0,0,0,0},
      {0,0,0,64,191,0,0,0,0,0,0},
      {0,0,0,64,16,0,0,0,0,0,0},
    };
    for(int y=std::max(top,472);y<std::min(top+rows,486);y++)for(int x=197;x<208;x++)
      pixel(x,y,known && x+.5f<fill_end?DARK:WHITE,bolt[y-472][x-197],true);
  } else {
    char number[8];if(!known)strcpy(number,"--");else snprintf(number,sizeof(number),"%d",pct);
    int pen=202-width(font_14,number)/2;
    for(const char* c=number;*c;c++) {
      const Glyph& g=font_14.glyphs[*c-32];
      for(int yy=0;yy<g.h;yy++)for(int xx=0;xx<g.w;xx++) {
        const int x=pen+g.x+xx,y=469+g.y+yy;
        pixel(x,y,known && x+.5f<fill_end?DARK:WHITE,font_14.data[g.offset+yy*g.w+xx],true);
      }
      pen+=g.advance;
    }
  }
}
static void header(const State &s) {
  char out[64];
  if (s.screen == Screen::Home) {
    time_t now = present_epoch;
    // Both workers used to redo timezone/calendar formatting for every tile.
    // The visible strings change once a minute. Each worker owns its cache.
    static thread_local time_t cached_minute=-1;
    static thread_local uint32_t cached_revision=UINT32_MAX;
    static thread_local char clock_text[16],date_text[64];
    if(cached_minute!=now/60 || cached_revision!=s.revision) {
      cached_minute=now/60;cached_revision=s.revision;
      tm t;localtime_r(&now,&t);
      if(now<1577836800){strcpy(clock_text,"--:--");strcpy(date_text,"Waiting for time");}
      else {strftime(clock_text,sizeof(clock_text),"%H:%M",&t);strftime(date_text,sizeof(date_text),"%A, %b %d",&t);}
    }
    home_connection(s);
    center(font_88, clock_text, 59, INK);
    center(font_14, date_text, 152, GREY);
  } else {
    const char *labels[] = {"Scout",      "Volume", "Brightness", "Wi-Fi",
                            "Microphone", "Timer",  "System"};
    center(font_28, labels[(int)s.screen], 50, INK);
    if (s.screen == Screen::Volume || s.screen == Screen::Brightness) {
      snprintf(out, sizeof(out), "%d%%", int(lroundf(display_value)));
      center(font_88, out, 312, CORAL);
    } else if (s.screen == Screen::Microphone) {
      center(font_28, s.muted ? "Mic off" : "Mic ready", 357,
             s.muted ? CORAL : CYAN);
    } else if (s.screen == Screen::Timer) {
      int sec = std::max(
          0, int((s.timer_end_us - present_now_us + 999999) / 1000000));
      snprintf(out, sizeof(out), "%02d:%02d", sec / 60, sec % 60);
      center(font_64, out, 323, GOLD);
    } else if (s.screen == Screen::Wifi) {
      const char *ssid = s.wifi_draft_ssid[0] ? s.wifi_draft_ssid : s.ssid;
      if (!ssid[0])
        ssid = "Find a network";
      // A 32-byte ASCII name must fit even when every glyph is a wide W.
      // The monospace font guarantees all of it fits in two safe-area lines.
      if(width(font_20,ssid)<=SCREEN_W-2*SAFE)center(font_20,ssid,94,CYAN);
      else wrapped(font_mono,ssid,94,CYAN,2);
    } else {
      center(font_28, s.wifi_connected ? "Wi-Fi connected" : "Wi-Fi offline", 326, CYAN);
      snprintf(out, sizeof(out), "Volume %d%% / Light %d%%", s.volume, s.brightness);
      center(font_20, out, 367, INK);
    }
  }
}
static void footer(const State &s) {
  if (s.screen == Screen::Home) {home_battery(s);return;}
  wrapped(font_20, s.sleeping ? "Resting" : s.caption, 407, INK, 1);
  const char *mode =
      s.sleeping ? "Lift or tap to wake" : s.muted ? "Microphone muted"
              : (s.voice_active ? s.conversation : "Voice not connected");
  char count[64];
  if (s.screen == Screen::Wifi && (s.wifi_password[0] || s.wifi_edit_index>=0)) {
    const int len=strlen(s.wifi_password);
    if(s.wifi_edit_index<0)snprintf(count,sizeof(count),"%d password characters",len);
    else if(s.wifi_edit_count==0)snprintf(count,sizeof(count),"%d characters / %d removed",len,s.wifi_edit_removed);
    else if(s.wifi_edit_count==1)snprintf(count,sizeof(count),"%d characters / changed %d",len,s.wifi_edit_index+1);
    else snprintf(count,sizeof(count),"%d characters / changed %d-%d",len,s.wifi_edit_index+1,s.wifi_edit_index+s.wifi_edit_count);
    mode = count;
  }
  center(font_14, mode, 433, s.muted ? CORAL : GREY);
}
static void details(const State &s) {
  if (s.wifi_draft_ssid[0]) {
    if (s.wifi_password[0] || s.wifi_edit_index>=0) {
      const int per = 24, len = strlen(s.wifi_password);
      int lines = std::max(1,(len + per - 1) / per);
      for (int offset = 0; offset < std::max(1,len); offset += per) {
        char part[25];
        int n = std::min(per, len - offset);
        memcpy(part, s.wifi_password + offset, n);
        part[n] = 0;
        int y = 366 - (lines - 1) * 29 + (offset / per) * 29;
        int x = (SCREEN_W - width(font_mono, part)) / 2;
        text(font_mono, part, x, y, INK);
        const int changed_first=std::max(offset,s.wifi_edit_index);
        const int changed_end=std::min(offset+n,s.wifi_edit_index+s.wifi_edit_count);
        if(s.wifi_edit_index>=0 && s.wifi_edit_count>0 && changed_first<changed_end) {
          char changed[25];const int count=changed_end-changed_first;
          memcpy(changed,part+(changed_first-offset),count);changed[count]=0;
          const int start=x+(changed_first-offset)*12;
          text(font_mono,changed,start,y,CORAL);
          for(int k=0;k<count*12;k++)for(int j=0;j<3;j++)pixel(start+k,y+25+j,CORAL);
        } else if(s.wifi_edit_index>=0 && s.wifi_edit_count==0 &&
          s.wifi_edit_index>=offset && (s.wifi_edit_index<offset+n || (s.wifi_edit_index==len && offset+n==len))) {
          // Deletion leaves a gap, including at the end or in an empty field.
          const int caret=x+(s.wifi_edit_index-offset)*12-2;
          for(int k=0;k<2;k++)for(int j=6;j<24;j++)pixel(caret+k,y+j,CORAL);
        }
      }
    } else
      center(font_20,
             s.wifi_connecting
                 ? "Connecting..."
                 : (s.wifi_connected ? "Connected" : s.wifi_error[0] ? s.wifi_error : "Say the password"),
             368, CYAN);
  } else if (s.network_count) {
    for (int i = 0; i < s.network_count; i++) {
      char part[40];
      fit_line(font_20,s.networks[i],part,sizeof(part));
      center(font_20, part, 282 + i * 28, i == 0 ? INK : GREY);
    }
  } else {
    const char *message = s.wifi_scanning      ? "Looking for networks"
                          : s.wifi_connecting  ? "Connecting..."
                          : s.wifi_connected   ? "Connected"
                          : s.wifi_error[0]    ? s.wifi_error
                          : s.wifi_scan_failed ? "Try scanning again"
                          : s.wifi_scan_done   ? "No networks nearby"
                                               : "Say a network name";
    center(font_20, message, 369, s.wifi_connected ? CYAN : GREY);
  }
}
static uint64_t clock_us(void *) { return esp_timer_get_time(); }
static void *fast(size_t n) {
  return heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static void *render_buffers[4];
void ui_reserve_memory() {
  // Reserve contiguous color/depth tiles before Wi-Fi and audio allocate
  // their small control structures. Ownership transfers to Viewer at begin.
  for (auto& buffer : render_buffers) {
    buffer = fast(SCREEN_W * SCENE_TILE_ROWS * sizeof(uint16_t));
    assert(buffer);
  }
}
static void *render_memory(size_t n) {
  if (n == SCREEN_W * SCENE_TILE_ROWS * sizeof(uint16_t)) {
    for (auto& buffer : render_buffers) if (buffer) {
      void* result = buffer;
      buffer = nullptr;
      return result;
    }
  }
  return fast(n);
}
static void *large(size_t n) {
  return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static void *geometry(size_t n) {
  if (n < 12000 && heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                           MALLOC_CAP_8BIT) > n + 100000) {
    void *p = fast(n);
    if (p)
      return p;
  }
  return large(n);
}
struct Pose {
  float t[3], r[4], s[3];
};
static Pose *previous;
static float transition_ms = 1000;
static std::atomic<bool> capture_pending{false};
static SemaphoreHandle_t capture_done;
static uint16_t *capture_pixels;
static void change_clip(a3d::Viewer &v, int clip) {
  auto &scene = v.scene();
  for (uint32_t n = 0; n < scene.nodeCount(); n++)
    scene.getNodeTRS(n, previous[n].t, previous[n].r, previous[n].s);
  transition_ms = 0;
  v.selectClip(clip);
  v.seek(0);
}
static State present_state;
static BlobRenderer blob;
static DittoListeningGlow listening_glow;
static DittoDamage scene_damage;
static bool stable_home_chrome=false;
static bool prepare_dizzy(void* user,const float* view,int width,int height) {
  auto& v=*static_cast<a3d::Viewer*>(user);
  listening_glow.prepare(v.binners()[0],v.scene().meshIndices(0),v.scene().meshTriangleCount(0),height,blob.listening_amount,blob.behavior.now,clock_us);
  bool ink=blob.projectInk(v.scene().worldMatrix(2),view,width,height);
  scene_damage.prepare(v,blob,listening_glow.level,stable_home_chrome,
    listening_glow.visible?&listening_glow.dirty_bounds:nullptr);
  return ink;
}
static void draw_dizzy(void*,a3d::SoftBackend& renderer,int top,int rows) {
  listening_glow.draw(renderer,top,rows);
  blob.drawInk(renderer,top,rows);
}
static TouchHistory::Cursor touch_cursor;
static void pose(void *user, a3d::SceneRuntime &scene, float dt) {
  const State& s = present_state;
  int64_t physics_start=esp_timer_get_time();
  auto motion=s.motion;
  if(physics_start-motion.time_us>150000)motion.valid=false;

  static float curious = 0, sad = 0, angry = 0, speaking = 0;
  const float response = 1.f - expf(-dt / 120.f);
  curious += ((s.expression == 1 ? 1.f : 0.f) - curious) * response;
  sad += ((s.expression == 2 ? 1.f : 0.f) - sad) * response;
  angry += ((s.expression == 3 ? 1.f : 0.f) - angry) * response;
  const float voice = !strcmp(s.conversation, "speaking")
      ? std::min(1.f, s.speaker_rms * 7.f) : 0.f;
  speaking += (voice - speaking) * (1.f - expf(-dt / (voice > speaking ? 35.f : 90.f)));
  auto alter = [&](int node, float sy, float rz, float ty) {
    if (node < 0 || uint32_t(node) >= scene.nodeCount()) return;
    Pose p; scene.getNodeTRS(node, p.t, p.r, p.s);
    p.s[1] *= sy; p.t[1] += ty;
    const float z = sinf(rz * .5f), w = cosf(rz * .5f);
    float q[4] = {w*p.r[0]-z*p.r[1], w*p.r[1]+z*p.r[0], w*p.r[2]+z*p.r[3], w*p.r[3]-z*p.r[2]};
    scene.setNodeTRS(node, p.t, q, p.s);
  };
  // Eye closure/expression arbitration is owned by BlobRenderer, after the
  // clip transition. UI moods do not independently squash or rotate eyes.
  alter(Avatar::EarLeft, 1, .13f*curious + .30f*sad, -.025f*sad);
  alter(Avatar::EarRight, 1, -.06f*curious - .30f*sad, -.025f*sad);
  // Mouth ownership is resolved with interaction/speech in BlobRenderer.
  transition_ms += dt;
  if (transition_ms < 280) {
    float f = transition_ms / 280;
    f = f * f * (3 - 2 * f);
    for (uint32_t n = 0; n < scene.nodeCount(); n++) {
      Pose target;
      scene.getNodeTRS(n, target.t, target.r, target.s);
      for (int k = 0; k < 3; k++) {
        target.t[k] = previous[n].t[k] + (target.t[k] - previous[n].t[k]) * f;
        target.s[k] = previous[n].s[k] + (target.s[k] - previous[n].s[k]) * f;
      }
      float q[4];
      a3d::quatNlerp(previous[n].r, target.r, f, q);
      scene.setNodeTRS(n, target.t, q, target.s);
    }
  }
  auto gravity=blob.gravityInBody(scene,motion.gravity);
  bool stepped=false;
  auto advance=[&](float seconds,Blob::Touch touch) {
    if(s.screen==Screen::Home) {touch.y-=SceneLayout::HomeShift;touch.press_y-=SceneLayout::HomeShift;}
    stepped=blob.advance(seconds,touch,s.sleeping,motion,&gravity,
        !strcmp(s.conversation,"speaking"),speaking)||stepped;
  };
  auto pick=[&](const Blob::Touch& touch) {
    auto& viewer=*static_cast<a3d::Viewer*>(user);
    if(s.frames)blob.pickTouch(touch,scene,viewer.binners()[0],0);
  };
  touch_cursor.begin(present_now_us,dt/1000.f);
  TouchHistory::Event event;
  while(board_touch_pop(present_now_us,event))touch_cursor.consume(event,advance,pick);
  touch_cursor.finish(present_now_us,advance);
  if(stepped) {
    xSemaphoreTake(state_mutex,portMAX_DELAY);state.physics_steps++;xSemaphoreGive(state_mutex);
  }
  blob.apply(scene);
  xSemaphoreTake(state_mutex,portMAX_DELAY);
  state.physics_ms=(esp_timer_get_time()-physics_start)/1000.f;
  state.physics_activity=blob.physics.activity();
  const auto &liquid=blob.physics.liquid;
  state.motion_x=liquid.x.p;state.motion_y=liquid.y.p;state.motion_z=liquid.z.p;
  state.motion_bend_x=liquid.bend_x.p;state.motion_bend_y=liquid.bend_y.p;state.motion_bend_z=liquid.bend_z.p;
  state.motion_spin_x=liquid.spin_x.p;state.motion_spin_y=liquid.spin_y.p;state.motion_spin_z=liquid.spin_z.p;state.motion_depth=liquid.depth.p;
  state.motion_wave={liquid.wave_x.p,liquid.wave_y.p,liquid.wave_z.p};
  const auto &material=blob.physics.material;
  state.material_scale={material.scale[0],material.scale[1],material.scale[2]};
  state.material_memory={material.memory[0],material.memory[1],material.memory[2]};
  state.material_shear={material.matrix[1],material.matrix[2],material.matrix[5]};
  state.material_support=material.support;state.material_gravity=material.gravity;
  state.material_volume=material.volume();
  state.annoyance=blob.physics.annoyance;
  state.slime_stretch=blob.physics.stretch();
  state.slime_press=blob.physics.dent.p;
  state.face_x_eyes=blob.eye_pose.cross;
  state.face_side_eye=std::abs(blob.eye_pose.gaze);
  state.face_gaze={blob.eye_pose.gaze,blob.eye_pose.gaze_y,0};
  state.emote_reaction_priority=blob.reaction_face;state.emote_rig_active=blob.rig.active;
  state.emote_seconds=blob.eye_seconds;
  state.emote_joint_motion=0;
  for(const auto& joint:blob.rig.pose.bone)state.emote_joint_motion=std::max({state.emote_joint_motion,
      std::abs(joint.rx),std::abs(joint.ry),std::abs(joint.rz),std::abs(joint.x),std::abs(joint.y),std::abs(joint.z)});
  state.face_oval=blob.presentation_face.mouth.oval;state.face_melon=blob.presentation_face.mouth.melon;
  state.face_dizzy=blob.presentation_face.dizzy;
  state.face_sleep=blob.behavior.pose.sleep;state.face_eye_open=blob.eye_pose.open[0];state.face_eye_width=blob.eye_pose.width;
  state.behavior_mode=int(blob.behavior.mode);state.behavior_contacts=blob.behavior.group;
  state.shake_active=liquid.recovery.shaking;state.shake_recovering=liquid.recovery.recovering;state.shake_damping=liquid.damping_scale;
  state.tap_depth=blob.physics.poke.p;state.body_squash=blob.physics.squash.p;
  state.tap_count=blob.physics.tap_count;
  state.tap_seen_press=blob.physics.seen_press;
  state.tap_hit=blob.physics.impact_hit;
  state.tap_local_x=blob.physics.impact_x;state.tap_local_y=blob.physics.impact_y;
  state.tap_bounce_x=blob.physics.bounce_x.p;state.tap_bounce_y=blob.physics.bounce_y.p;
  state.tap_bounce_z=blob.physics.bounce_z.p;state.tap_ripple=blob.physics.ripple.p;
  xSemaphoreGive(state_mutex);
}

// Only the command task owns this transfer. The UI publishes one immutable
// snapshot through capture_done, then keeps rendering independently. A timed-
// out request drains its old UI publication before another capture may start.
static char* capture_request_id;
static int capture_row=-1;
static int64_t capture_deadline_us;
static bool capture_failed;
static void capture_error(const char* id,const char* message) {
  cJSON* r=cJSON_CreateObject();
  cJSON_AddStringToObject(r,"id",id?id:"");
  cJSON_AddBoolToObject(r,"ok",false);
  cJSON_AddStringToObject(r,"error",message);emit(r);
}
static void capture_release() {
  heap_caps_free(capture_request_id);capture_request_id=nullptr;
  capture_row=-1;capture_failed=false;
}
bool ui_capture_busy(){return capture_request_id!=nullptr;}
void ui_capture(const char* id) {
  if(ui_capture_busy()){capture_error(id,"A display capture is already in progress");return;}
  if(!capture_done || !capture_pixels){capture_error(id,"Display capture is not ready");return;}
  const char* name=id?id:"";
  capture_request_id=static_cast<char*>(heap_caps_malloc(strlen(name)+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!capture_request_id){capture_error(id,"Could not allocate capture request");return;}
  strcpy(capture_request_id,name);capture_row=-1;capture_failed=false;
  capture_deadline_us=esp_timer_get_time()+5000000;
  xSemaphoreTake(ui_mutex,portMAX_DELAY);
  xSemaphoreTake(capture_done,0);
  capture_pending.store(true,std::memory_order_release);
  xSemaphoreGive(ui_mutex);
}
bool ui_capture_step() {
  if(!ui_capture_busy())return false;
  if(capture_row<0) {
    if(xSemaphoreTake(capture_done,0)!=pdTRUE) {
      if(!capture_failed && esp_timer_get_time()>=capture_deadline_us) {
        capture_error(capture_request_id,"Display did not produce a capture");
        capture_failed=true;return true;
      }
      return false;
    }
    if(capture_failed){capture_release();return false;}
    capture_row=0;
  }
  const int h=std::min(2,SCREEN_H-capture_row);
  char encoded[2200];size_t n=0;
  if(mbedtls_base64_encode(reinterpret_cast<unsigned char*>(encoded),sizeof(encoded),&n,
      reinterpret_cast<const unsigned char*>(capture_pixels+capture_row*SCREEN_W),SCREEN_W*h*2)!=0) {
    capture_error(capture_request_id,"Could not encode display capture");capture_release();return true;
  }
  encoded[n]=0;
  cJSON* o=cJSON_CreateObject();
  cJSON_AddStringToObject(o,"event","display.capture");
  cJSON_AddStringToObject(o,"id",capture_request_id);
  cJSON_AddNumberToObject(o,"y",capture_row);
  cJSON_AddNumberToObject(o,"rows",h);
  cJSON_AddStringToObject(o,"pixels",encoded);emit(o);
  capture_row+=h;
  if(capture_row==SCREEN_H) {
    cJSON* r=cJSON_CreateObject();
    cJSON_AddStringToObject(r,"id",capture_request_id);
    cJSON_AddBoolToObject(r,"ok",true);
    cJSON* result=cJSON_AddObjectToObject(r,"result");
    cJSON_AddNumberToObject(result,"width",SCREEN_W);
    cJSON_AddNumberToObject(result,"height",SCREEN_H);
    cJSON_AddStringToObject(result,"format","RGB565LE");emit(r);
    capture_release();
  }
  return true;
}
static SemaphoreHandle_t native_tile_mutex;
static unsigned native_tile_index;
static int64_t native_compose_us, native_text_us, native_upload_us;
static thread_local int64_t tile_compose_us, tile_text_us;
static uint16_t scene_row_fade[SCREEN_H];
static uint16_t *sleep_background,*sleep_patch;
static bool sleep_background_valid=false;
static int64_t sleep_origin_us=0;
static void native_band(const uint16_t *source, int y, int h,float sleep_seconds=-1) {
  xSemaphoreTake(native_tile_mutex, portMAX_DELAY);
  top = y;
  rows = h;
  canvas_left=0;canvas_stride=SCREEN_W;
  canvas = board_tile_buffer(native_tile_index++ & 1);
  int64_t stage = esp_timer_get_time();
  if (!source)
    memset(canvas, 0, SCREEN_W * h * 2);
  else
    memcpy(canvas, source, SCREEN_W * h * sizeof(uint16_t));
  native_compose_us += esp_timer_get_time() - stage;
  stage = esp_timer_get_time();
  if(sleep_seconds>=0)DittoSleepMarks::draw(canvas,y,h,sleep_seconds,1,HOME_SLEEP_MARK_OFFSET,true);
  if (y < HOME_HEADER_BOTTOM ||
      (present_state.screen != Screen::Home && y + h > 310 && y < 405))
    header(present_state);
  if (present_state.screen == Screen::Wifi && y + h > 275 && y < 400)
    details(present_state);
  if (y + h > 407)
    footer(present_state);
  native_text_us += esp_timer_get_time() - stage;
  stage = esp_timer_get_time();
  board_present_tile(y, h, canvas, false);
  native_upload_us += esp_timer_get_time() - stage;
  xSemaphoreGive(native_tile_mutex);
}
static int scene_offset() {
  return 0; // Framing lives in the camera; tiles cover every physical row.
}
static void native_tile(void *, const uint16_t *p, int, int y, int, int h) {
  const int offset = scene_offset();
  int first = std::max(SceneLayout::Top, y + offset), end = std::min(SceneLayout::Bottom, y + offset + h);
  if (first >= end) return;
  int64_t stage = esp_timer_get_time();
  board_present_tile(first, end - first,
      const_cast<uint16_t *>(p + (first - y - offset) * SCREEN_W), true,nullptr,
      scene_damage.usable?&scene_damage.bounds:nullptr);
  const int64_t upload=esp_timer_get_time()-stage;
  xSemaphoreTake(native_tile_mutex, portMAX_DELAY);
  native_compose_us += tile_compose_us;
  native_text_us += tile_text_us;
  native_upload_us += upload;
  xSemaphoreGive(native_tile_mutex);
}
static void native_overlay(void *, uint16_t *p, int y, int h, int, int) {
  const int offset = scene_offset();
  int first = std::max(SceneLayout::Top, y + offset), end = std::min(SceneLayout::Bottom, y + offset + h);
  if (first >= end) return;
  top = first;
  rows = end - first;
  canvas_left=0;canvas_stride=SCREEN_W;
  canvas = p + (first - y - offset) * SCREEN_W;
  int64_t stage = esp_timer_get_time();
  if(present_state.sleeping && present_state.screen==Screen::Home && blob.behavior.sleep_time>=1.9f)
    memcpy(sleep_background+first*SCREEN_W,canvas,rows*SCREEN_W*2);
  DittoSleepMarks::draw(canvas,first,rows,blob.behavior.sleep_time,blob.behavior.pose.sleep,
      present_state.screen==Screen::Home?HOME_SLEEP_MARK_OFFSET:offset,
      present_state.screen==Screen::Home);
  // Home keeps the entire glossy body visible. Settings retain their existing
  // text contrast treatment; native typography is composed above the scene.
  tile_compose_us = esp_timer_get_time() - stage;
  stage = esp_timer_get_time();
  if (first < HOME_HEADER_BOTTOM || (present_state.screen != Screen::Home &&
                      end > 310 && first < 405)) header(present_state);
  if (present_state.screen == Screen::Wifi && end > 275 && first < 400)
    details(present_state);
  if (end > 407) footer(present_state);
  tile_text_us = esp_timer_get_time() - stage;
}
static void sleep_region(int x,int y,int w,int h,float seconds) {
  using Pair=uint32_t __attribute__((__may_alias__));
  for(int row=0;row<h;row++) {
    const auto* src=reinterpret_cast<const Pair*>(sleep_background+(y+row)*SCREEN_W+x);
    auto* dst=reinterpret_cast<Pair*>(sleep_patch+row*w);
    for(int i=0;i<w/2;i++)dst[i]=src[i];
  }
  DittoSleepMarks::draw(sleep_patch,y,h,seconds,1,HOME_SLEEP_MARK_OFFSET,true,x,w);
  canvas=sleep_patch;canvas_left=x;canvas_stride=w;top=y;rows=h;
  if(y<HOME_HEADER_BOTTOM)header(present_state);
  if(y+h>HOME_BATTERY_TOP)footer(present_state);
  board_present_patch(x,y,w,h,sleep_patch);
}
void ui_task(void *) {
  native_tile_mutex = xSemaphoreCreateMutex();
  assert(native_tile_mutex);
  a3d::Display d;
  d.width = SCREEN_W;
  d.height = SCREEN_H;
  d.sendTile = native_tile;
  a3d::FreeRtosExecutor executor(2, 4096, 2);
  a3d::Viewer v;
  v.setExecutor(executor);
  v.setAllocator(render_memory, free);
  v.setLargeAllocator(geometry, free);
  v.setClock(clock_us, nullptr);
  v.setOverlay(native_overlay, nullptr);
  v.setBackgroundPainter([](void*,uint16_t* tile,int y,int h,int w){
    return listening_glow.paintBackground(tile,y,h,w);
  },nullptr);
  v.setTileNeeded([](void*,int y,int h){
    return !scene_damage.usable || board_scene_rows_needed(y,h,scene_damage.bounds);
  },nullptr);
  v.setGeometryOverlay(prepare_dizzy,draw_dizzy,&v);
  a3d::ViewerConfig cfg;
  cfg.tileHeight = SCENE_TILE_ROWS;
  cfg.texture = a3d::TextureMode::None;
  cfg.culling = -1;
  cfg.visibleRowBegin = 0;
  cfg.visibleRowEnd = SCREEN_H;
  if (!v.begin(d, cfg) || !v.openAsset(avatar_start, avatar_end - avatar_start)) {
    event_message("renderer.error", v.error());
    vTaskDelete(nullptr);
    return;
  }
  if(!blob.initialize(v.scene(),large)) {
    event_message("renderer.error","Blob deformation allocation failed"); vTaskDelete(nullptr); return;
  }
  // The native SoftBackend rasterizes the binner's exact projected floats.
  // Exclude tiles containing no candidate pixel centres before shader setup.
  for(uint32_t i=0;i<v.drawableCount();i++)v.binners()[i].setPixelCenterBounds(true);
  blob.executor=&executor;
  previous = (Pose *)large(v.scene().nodeCount() * sizeof(Pose));
  assert(previous);
  const int material_count = v.image().materialCount();
  // Only the tiny face meshes use these immutable palettes. Keep their exact
  // colors in PSRAM to free internal memory for prompt touch acquisition.
  auto* ramps = (a3d::SoftBackend::ColorRamp*)large(
      (material_count-1) * sizeof(a3d::SoftBackend::ColorRamp));
  assert(ramps || material_count<=1);
  for (int i = 1; i < material_count; i++) {
    const auto& material = v.image().materials()[i];
    a3d::SoftBackend::makeSmoothColorRamp(ramps[i-1], material.color[0],
                                    material.color[1], material.color[2]);
  }
  for (int i = 0; i < 2; i++) v.backend(i).setColorRamps(ramps, material_count-1);
  auto* liquid_light=(a3d::SoftBackend::SurfaceMap*)large(sizeof(a3d::SoftBackend::SurfaceMap));
  assert(liquid_light);
  const auto& pigment=v.image().materials()[0];
  DittoLighting::build(*liquid_light,pigment.color[0],pigment.color[1],pigment.color[2]);
  for(int i=0;i<2;i++)v.backend(i).setSurfaceMap(liquid_light);
  v.selectClip(Avatar::Idle);
  v.setPoseCallback(pose, &v);
  v.setLightDirection(-.3f, -.7f, -1);
  for (int y = 0; y < SCREEN_H; y++) {
    int gy = y, light = 256;
    if (gy < 166) light = std::clamp((gy - 96) * 256 / 70, 0, 256);
    if (gy > 290) {
      // Quintic easing: zero slope and curvature at both ends, no visible
      // horizontal knee where the old linear fade abruptly began.
      float f=std::clamp((gy-290)/134.f,0.f,1.f);
      float ease=f*f*f*(f*(f*6-15)+10);
      light=std::min(light,int(256*(1-ease)+.5f));
    }
    scene_row_fade[y] = light;
  }
  for (int i = 0; i < 2; i++) v.backend(i).setSmoothRowFade(scene_row_fade);
  capture_pixels = (uint16_t *)large(SCREEN_W * SCREEN_H * 2);
  sleep_background=(uint16_t*)heap_caps_calloc(SCREEN_W*SCREEN_H,2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  sleep_patch=(uint16_t*)large(76*104*2);
  assert(sleep_background && sleep_patch);
  capture_done = xSemaphoreCreateBinary();
  assert(capture_pixels && capture_done);
  // Static aura levels belong in PSRAM; keep the frequently sampled material
  // row in internal RAM instead. Failure of this optional cache is harmless.
  assert(listening_glow.begin(large,large));
  auto* hot_surface=static_cast<uint16_t*>(fast(512*4*sizeof(uint16_t)));
  if(hot_surface) {
    memcpy(hot_surface,liquid_light->colors+256*a3d::SoftBackend::SurfaceMap::RowStride,512*4*sizeof(uint16_t));
    for(int i=0;i<2;i++)v.backend(i).setSurfaceHotRow(hot_surface);
  }
  v.setOrbit(.12f, .05f, 3.9f);
  event_message("renderer.ready", "Native live avatar ready");
  uint32_t serial = 0, revision = UINT32_MAX, measured = 0;
  int current = Avatar::Idle, count = 0;
  bool gesture = false;
  DittoAutoplay::Scheduler autoplay(esp_random());
  char last_conversation[16] = {};
  bool last_idle = false;
  float elapsed = 0, yaw = .12f, pitch = .05f, times[120] = {};
  int64_t last = esp_timer_get_time(), last_stats = last, last_frame = 0;
  int applied_brightness = -1;
  BrightnessTransition brightness;
  // Hold full speed through the sleep pose transition; release only once
  // rendering has settled, and reacquire before the first waking frame.
  esp_pm_lock_handle_t render_power=power_render_lock();
  bool render_boost=true;
  Screen last_screen = Screen::Home;
  float reveal_ms = 300;
  TickType_t wake = xTaskGetTickCount();
  time_t last_minute = -1;
  int64_t next_sleep_overlay=0;
  int64_t last_charge_tick=-1;
  for (;;) {
    int64_t start = esp_timer_get_time();
    State s = snapshot();
    // A confirmed helper release ends Listening immediately, even while
    // macOS drains its USB AudioQueue. Other microphone clients still work.
    const bool remote_listening=s.remote_dictating && start<s.remote_dictation_until_us;
    const bool usb_listening=(remote_listening || (scout_usb_recording() &&
        start>=s.remote_audio_tail_until_us))&&!s.muted&&!s.sleeping;
    if(usb_listening&&!s.voice_active)strcpy(s.conversation,"listening");
    bool idle = s.sleeping;
    float dt = std::min(100.f, (start - last) / 1000.f);
    last = start;
    bool boost=!idle || blob.behavior.sleep_time<1.9f || !last_idle;
    if(boost!=render_boost) {
      if(boost)esp_pm_lock_acquire(render_power);else esp_pm_lock_release(render_power);
      render_boost=boost;
    }
    const int target_brightness=brightness.update(idle,s.brightness,start);
    if (applied_brightness != target_brightness) {
      board_brightness(target_brightness);
      applied_brightness=target_brightness;
    }
    if(idle && last_idle && blob.behavior.sleep_time>=1.9f && sleep_background_valid &&
        s.screen==Screen::Home && last_screen==Screen::Home && s.revision==revision) {
      bool capture=capture_pending.load(std::memory_order_acquire);
      time_t epoch=time(nullptr);bool minute_changed=epoch/60!=last_minute;
      bool status_changed=s.usb_connected!=present_state.usb_connected || s.bluetooth_connected!=present_state.bluetooth_connected || home_battery_changed(s,present_state);
      const bool charging_frame=s.battery_charging&&home_charge_tick(start)!=last_charge_tick;
      xSemaphoreTake(state_mutex,portMAX_DELAY);
      state.fps=0;state.physics_ms=0;state.render_parked=true;state.sleep_overlay_active=true;
      xSemaphoreGive(state_mutex);
      if(start>=next_sleep_overlay || minute_changed || capture || status_changed) {
        present_state=s;present_epoch=epoch;present_now_us=start;ui_alpha=255;
        float seconds=float(start-sleep_origin_us)*1e-6f;
        xSemaphoreTake(ui_mutex,portMAX_DELAY);
        board_present_begin(capture,true);
        // Restore the clean cached image before each overlay. Only these small
        // bands are composed; the presenter transfers only changed rectangles.
        if(minute_changed || status_changed)for(int y=0;y<HOME_HEADER_BOTTOM;y+=16)
          native_band(sleep_background+y*SCREEN_W,y,16,seconds);
        if(status_changed || charging_frame)
          sleep_region(180,HOME_BATTERY_TOP,50,HOME_BATTERY_BOTTOM-HOME_BATTERY_TOP,seconds);
        sleep_region(268,150,76,104,seconds);
        sleep_region(180,276,40,56,seconds);
        board_present_end();
        if(capture) {
          memcpy(capture_pixels,board_framebuffer(),SCREEN_W*SCREEN_H*2);
          capture_pending.store(false,std::memory_order_release);xSemaphoreGive(capture_done);
        }
        xSemaphoreGive(ui_mutex);
        xSemaphoreTake(state_mutex,portMAX_DELAY);
        state.sleep_overlay_frames++;state.sleep_overlay_ms=(esp_timer_get_time()-start)/1000.f;
        xSemaphoreGive(state_mutex);
        last_minute=epoch/60;next_sleep_overlay=start+83333; // 12 Hz for very slow motion.
        last_charge_tick=home_charge_tick(start);
      }
      measured=0;last_stats=start;
      vTaskDelay(pdMS_TO_TICKS(10));wake=xTaskGetTickCount();continue;
    }
    if(!idle)sleep_background_valid=false;
    xSemaphoreTake(state_mutex,portMAX_DELAY);state.sleep_overlay_active=false;xSemaphoreGive(state_mutex);
    if (idle && last_idle && blob.behavior.sleep_time >= 1.9f && !capture_pending.load(std::memory_order_acquire) &&
        s.revision == revision && time(nullptr)/60 == last_minute &&
        (s.screen!=Screen::Timer || start-last_frame<1000000)) {
      xSemaphoreTake(state_mutex,portMAX_DELAY);
      state.fps=0;state.physics_ms=0;state.render_parked=true;
      xSemaphoreGive(state_mutex);
      vTaskDelay(pdMS_TO_TICKS(brightness.active(start)?20:40));
      wake = xTaskGetTickCount();
      continue;
    }
    xSemaphoreTake(state_mutex,portMAX_DELAY);state.render_parked=false;xSemaphoreGive(state_mutex);
    last_frame = start;
    const int resting = idle ? Avatar::Sleep
        : !strcmp(s.conversation, "listening") ? Avatar::Listening
        : !strcmp(s.conversation, "thinking") ? Avatar::Thinking
        : !strcmp(s.conversation, "speaking") ? Avatar::Speaking : Avatar::Idle;
    int desired_clip = current;
    if(usb_listening){gesture=false;s.clip=Avatar::Idle;}
    const bool manual_emote=serial!=s.clip_serial;
    if (manual_emote) {
      serial = s.clip_serial;
      desired_clip = s.clip == Avatar::Idle ? resting : s.clip;
      gesture = s.clip != Avatar::Idle;
      elapsed = 0;
      change_clip(v, desired_clip);
    } else {
      if (strcmp(last_conversation, s.conversation) || last_idle != idle ||
          (gesture && elapsed >= v.clipDurationMs())) gesture = false;
      if (!gesture) desired_clip = resting;
      if (desired_clip != current) {
        elapsed = 0;
        change_clip(v, desired_clip);
      }
    }
    const bool auto_available=s.screen==Screen::Home && !idle && !s.touch.down &&
        !s.voice_active && !usb_listening &&
        (!strcmp(s.conversation,"idle") || !strcmp(s.conversation,"offline"));
    const auto auto_decision=autoplay.tick(start,auto_available,gesture || s.motion_active,
        s.remote_reaction_serial,manual_emote);
    if(auto_decision.cancel) {gesture=false;desired_clip=resting;}
    if(auto_decision.clip>=0) {gesture=true;desired_clip=auto_decision.clip;elapsed=0;}
    if(auto_decision.cancel || auto_decision.clip>=0)change_clip(v,desired_clip);
    current = desired_clip;
    snprintf(last_conversation, sizeof(last_conversation), "%s", s.conversation);
    last_idle = idle;
    if (!idle) {
      elapsed += dt;
      float aim_yaw = .12f, aim_pitch = .05f;
      if (!strcmp(s.conversation, "thinking")) {
        aim_yaw += .09f * sinf(start / 850000.f);
        aim_pitch += .055f;
      } else if (!strcmp(s.conversation, "listening"))
        aim_pitch -= .035f;
      float response = 1.f - expf(-dt / 400.f);
      yaw += (aim_yaw - yaw) * response;
      pitch += (aim_pitch - pitch) * response;
      v.setOrbit(yaw, pitch, 3.9f);
      blob.view_yaw=yaw;blob.view_pitch=pitch;
    }
    if (s.screen != last_screen) {
      reveal_ms = 0;
      display_value = s.screen == Screen::Volume ? s.volume : s.brightness;
      last_screen = s.screen;
    }
    int old_alpha = ui_alpha;
    reveal_ms = std::min(260.f, reveal_ms + dt);
    ui_alpha = int(255 * std::min(1.f, .35f + reveal_ms / 400.f));
    float desired = s.screen == Screen::Volume ? s.volume : s.brightness;
    display_value += (desired - display_value) * std::min(1.f, dt / 80.f);
    present_epoch = time(nullptr);
    present_now_us = start;
    time_t minute = present_epoch / 60;
    bool static_changed = revision != s.revision || minute != last_minute ||
                          old_alpha != ui_alpha ||
                          s.usb_connected!=present_state.usb_connected ||
                          s.bluetooth_connected!=present_state.bluetooth_connected ||
                          home_battery_changed(s,present_state);
    last_minute = minute;
    revision = s.revision;
    present_state = s;
    v.setScreenShift(0,s.screen==Screen::Home?SceneLayout::HomeOffset:SceneLayout::StandardOffset);
    for (int i = 0; i < 2; i++)
      v.backend(i).setSmoothRowFade(s.screen == Screen::Home ? nullptr : scene_row_fade);
    native_compose_us = native_text_us = native_upload_us = 0;
    xSemaphoreTake(ui_mutex, portMAX_DELAY);
    const bool capturing = capture_pending.load(std::memory_order_acquire);
    static_changed |= capturing;
    stable_home_chrome=s.screen==Screen::Home && !s.sleeping && !static_changed &&
      (!s.battery_charging || home_charge_tick(start)==last_charge_tick);
    board_present_begin(capturing);
    if (static_changed)
      for (int y = 0; y < SceneLayout::Top; y += 16)
        native_band(nullptr, y, 16);
    last_charge_tick=home_charge_tick(start);
    blob.setEmote(current,elapsed/1000.f,autoplay.owns && autoplay.reaction);
    v.frame(idle && blob.behavior.sleep_time >= 1.9f ? 0 : dt);
    if(idle && s.screen==Screen::Home && blob.behavior.sleep_time>=1.9f) {
      sleep_background_valid=true;sleep_origin_us=start-int64_t(blob.behavior.sleep_time*1e6f);
      next_sleep_overlay=0;
    }
    int64_t finish = esp_timer_get_time();
    board_present_end();
    native_upload_us += esp_timer_get_time() - finish;
    if (capturing) {
      // This stable buffer is held until the next serial capture request.
      // Copy only after every tile's DMA transfer has completed.
      memcpy(capture_pixels, board_framebuffer(), SCREEN_W * SCREEN_H * 2);
      capture_pending.store(false, std::memory_order_release);
      xSemaphoreGive(capture_done);
    }
    xSemaphoreGive(ui_mutex);
    const auto &stats = v.stats();
    float ms = (esp_timer_get_time() - start) / 1000.f;
    times[count++ % 120] = ms;
    measured++;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.render_clip = current;
    state.auto_hello_count=autoplay.hello_count;
    state.auto_reaction_count=autoplay.reaction_count;
    state.auto_hello_at_us=autoplay.hello_at;
    state.glow_mask_ms=listening_glow.mask_us/1000.f;
    state.glow_blur_ms=listening_glow.blur_us/1000.f;
    state.glow_palette_ms=listening_glow.palette_us/1000.f;
    state.anim_ms = stats.animUs / 1000.f;
    state.skin_ms = stats.skinUs / 1000.f;
    state.bin_ms = stats.binUs / 1000.f;
    state.draw_ms = stats.drawUs / 1000.f;
    state.raster0_ms = stats.rasterUs[0] / 1000.f;
    state.raster1_ms = stats.rasterUs[1] / 1000.f;
    state.compose_ms = native_compose_us / 1000.f;
    state.text_ms = native_text_us / 1000.f;
    state.upload_ms = native_upload_us / 1000.f;
    state.frames++;
    state.frame_ms = ms;
    if (ms <= 25.f) state.frames_within25++;
    if (ms > 33.333f)
      state.missed_frames++;
    if (start - last_stats >= 1000000) {
      state.ui_stack_free = uxTaskGetStackHighWaterMark(nullptr);
      auto helper = xTaskGetHandle("a3d_job");
      state.render_stack_free =
          helper ? uxTaskGetStackHighWaterMark(helper) : 0;
      float sorted[120];
      int n = std::min(count, 120);
      memcpy(sorted, times, n * sizeof(float));
      std::sort(sorted, sorted + n);
      state.frame_p95_ms = sorted[(n - 1) * 95 / 100];
      state.fps = measured * 1e6 / (start - last_stats);
      measured = 0;
      last_stats = start;
    }
    xSemaphoreGive(state_mutex);
    if (idle && blob.behavior.sleep_time >= 1.9f) {
      vTaskDelay(pdMS_TO_TICKS(100));
      wake = xTaskGetTickCount();
    } else if (ms > 32) {
      vTaskDelay(1);
      wake = xTaskGetTickCount();
    } else
      vTaskDelayUntil(&wake, pdMS_TO_TICKS(33));
  }
}
