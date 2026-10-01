"""Exercise production text/layout against the actual font masks and safe area."""
from pathlib import Path
import subprocess,os,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from storage_hygiene import scratch_directory
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'main/ui.cpp').read_text();body=s[s.index('static constexpr uint16_t INK'):s.index('static uint64_t clock_us')]
needle='static void text(const Font &f, const char *s, int x, int y, uint16_t c) {'
assert needle in body
body=body.replace(needle,needle+r'''
  calls.push_back(s);
  int pen=x;
  for(const char* p=s;*p;p++) {
    unsigned char ch=*p;if(ch<32||ch>126)ch='?';const auto&g=f.glyphs[ch-32];
    for(int yy=0;yy<g.h;yy++)for(int xx=0;xx<g.w;xx++)if(f.data[g.offset+yy*g.w+xx])
      assert(pen+g.x+xx>=SAFE && pen+g.x+xx<SCREEN_W-SAFE && y+g.y+yy>=SAFE && y+g.y+yy<SCREEN_H-SAFE);
    pen+=g.advance;
  }
''')
prefix=r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include "fonts.h"
#include "scene_layout.h"
#include "battery_status.h"
constexpr int SCREEN_W=410,SCREEN_H=502,SAFE=50;
enum class Screen{Home,Volume,Brightness,Wifi,Microphone,Timer,System};
struct State {
 uint32_t revision=0;
 Battery::Mode battery_mode=Battery::Mode::Charging;bool battery_external=true;
 int battery_percent=100;bool battery_charging=true,usb_connected=true,bluetooth_connected=false;
 Screen screen=Screen::Wifi;bool sleeping=false,muted=false,voice_active=false,wifi_connecting=false,wifi_connected=false,wifi_scanning=false,wifi_scan_done=false,wifi_scan_failed=false;
 char ssid[33]={},wifi_draft_ssid[33]={},wifi_password[65]={},wifi_error[64]={},caption[128]="Review password",conversation[16]="idle",networks[4][33]={};
 int wifi_edit_index=-1,wifi_edit_count=0,wifi_edit_removed=0,network_count=0,volume=100,brightness=100;int64_t timer_end_us=86400000000LL;
};
static std::vector<std::string> calls;
'''
suffix=r'''
int main(){
 std::vector<uint16_t> frame(410*502);canvas=frame.data();rows=502;top=0;ui_alpha=255;
 State s;strcpy(s.wifi_draft_ssid,"Prototype review");
 auto draw=[&](){std::fill(frame.begin(),frame.end(),0);calls.clear();header(s);details(s);footer(s);};
 int cases=0;
 for(int ch=33;ch<=126;ch++){
  memset(s.wifi_draft_ssid,ch,32);s.wifi_draft_ssid[32]=0;draw();
  calls.clear();header(s);int seen=0;for(size_t i=1;i<calls.size();i++)seen+=calls[i].size();assert(seen==32);
  char fit[40];fit_line(font_20,s.wifi_draft_ssid,fit,sizeof(fit));assert(width(font_20,fit)<=310);
  if(width(font_20,s.wifi_draft_ssid)>310)assert(strstr(fit,"...")&&fit[0]==ch&&fit[strlen(fit)-1]==ch);
  cases++;
 }
 strcpy(s.wifi_draft_ssid,"Prototype review");
 for(int len=0;len<=63;len++)for(int index=0;index<=len;index++){
  memset(s.wifi_password,'M',len);s.wifi_password[len]=0;s.wifi_edit_index=index;s.wifi_edit_count=0;s.wifi_edit_removed=2;draw();
  int coral=std::count(frame.begin(),frame.end(),CORAL);assert(coral==36);cases++;
  if(index<len){s.wifi_edit_count=std::min(7,len-index);draw();assert(std::count(frame.begin(),frame.end(),CORAL)>=36*s.wifi_edit_count);cases++;}
 }
 s={};s.network_count=4;for(auto& network:s.networks){memset(network,'W',32);network[32]=0;}draw();
 s.network_count=0;
 for(const char* message:{"Connecting...","Check the Wi-Fi password","Use at least 8 characters","Choose a network first","Couldn't scan","Connection timed out"}){strcpy(s.caption,message);strcpy(s.wifi_error,message);draw();cases++;}
 for(auto screen:{Screen::Home,Screen::Volume,Screen::Brightness,Screen::Microphone,Screen::Timer,Screen::System}){s.screen=screen;display_value=100;present_epoch=1789990000;draw();cases++;}
 for(int pct:{-1,0,9,15,25,64,78,99,100})for(int link=0;link<3;link++)for(bool charging:{false,true})for(int phase:{0,6,12,18,24,30}){
  present_now_us=phase*83333LL;
  s.screen=Screen::Home;s.battery_percent=pct;s.usb_connected=link==1;s.bluetooth_connected=link==2;s.battery_charging=charging;s.battery_mode=charging?Battery::Mode::Charging:Battery::Mode::Discharging;s.battery_external=charging;draw();cases++;
  std::fill(frame.begin(),frame.end(),0);header(s);footer(s);const auto whole=frame;
  // The same chrome must survive both live 18-row and parked 16-row bands.
  for(int band:{16,18}){
   std::fill(frame.begin(),frame.end(),0);
   for(int y=0;y<502;y+=band){canvas=frame.data()+y*410;top=y;rows=std::min(band,502-y);header(s);footer(s);}
   assert(frame==whole);canvas=frame.data();top=0;rows=502;
  }
  for(int y=0;y<502;y++)for(int x=0;x<410;x++)
   if((x<SAFE||x>=410-SAFE||y<SAFE||y>=SCREEN_H-SAFE) &&
      !(x>=STATUS_EDGE_LEFT && x<STATUS_EDGE_RIGHT &&
        ((y>=32 && y<58) || (y>=HOME_BATTERY_TOP && y<HOME_BATTERY_BOTTOM))))assert(whole[y*410+x]==0);
  // Time-dependent ink stays in the battery only; disconnected/not-charging
  // states stay still. The old text area beside the connection icon is empty.
  present_now_us+=6*83333LL;std::fill(frame.begin(),frame.end(),0);header(s);footer(s);
  int changed=0;
  for(int y=0;y<502;y++)for(int x=0;x<410;x++){
   if(y>=50&&y<77&&x>=81&&x<118)assert(frame[y*410+x]==0);
   if(frame[y*410+x]!=whole[y*410+x]){assert(charging&&x>=181&&x<223&&y>=HOME_BATTERY_TOP&&y<HOME_BATTERY_BOTTOM);changed++;}
  }
  if(charging && pct>0 && phase==0)assert(changed>0);
  if(!charging)assert(changed==0);
 }

 // Parked standby redraws only the battery patch. Every transition must erase
 // old bolt/fill ink and match a complete composition, even on a bright body.
 for(auto before:{Battery::Mode::Discharging,Battery::Mode::Charging,Battery::Mode::Full,Battery::Mode::Paused,Battery::Mode::Absent,Battery::Mode::Unknown})
 for(auto after:{Battery::Mode::Discharging,Battery::Mode::Charging,Battery::Mode::Full,Battery::Mode::Paused,Battery::Mode::Absent,Battery::Mode::Unknown}) {
  s.screen=Screen::Home;s.battery_percent=78;s.battery_mode=before;s.battery_external=before!=Battery::Mode::Discharging;
  std::fill(frame.begin(),frame.end(),uint16_t(0x9215));canvas=frame.data();canvas_left=0;canvas_stride=410;top=0;rows=502;footer(s);
  const auto old=frame;
  s.battery_mode=after;s.battery_external=after!=Battery::Mode::Discharging;
  std::fill(frame.begin(),frame.end(),uint16_t(0x9215));footer(s);const auto target=frame;
  frame=old;std::vector<uint16_t> patch(50*22,0x9215);canvas=patch.data();canvas_left=180;canvas_stride=50;top=468;rows=22;footer(s);
  for(int y=0;y<22;y++)std::copy_n(patch.data()+y*50,50,frame.data()+(468+y)*410+180);
  assert(frame==target);cases++;
  canvas=frame.data();canvas_left=0;canvas_stride=410;top=0;rows=502;
 }

 s.screen=Screen::Home;s.battery_percent=100;s.battery_mode=Battery::Mode::Full;s.battery_external=true;
 std::fill(frame.begin(),frame.end(),uint16_t(0x9215));footer(s);const auto full=frame;
 present_now_us+=1000000;
 std::fill(frame.begin(),frame.end(),uint16_t(0x9215));footer(s);assert(frame==full);cases++;
 // Full on external power retains the internal bolt, with no charging sweep.
 for(int y=472;y<486;y++)for(int x=188;x<197;x++)assert(full[y*410+x]!=0x1082);
 s.battery_mode=Battery::Mode::Discharging;s.battery_external=false;
 std::fill(frame.begin(),frame.end(),uint16_t(0x9215));footer(s);assert(frame!=full);const auto unplugged=frame;cases++;
 s.battery_mode=Battery::Mode::Full;
 std::fill(frame.begin(),frame.end(),uint16_t(0x9215));footer(s);assert(frame==unplugged);cases++;
 for(int y=0;y<502;y++)for(int x=0;x<410;x++)
  if(!(y>=468&&y<490&&((x>=181&&x<223)||(x>=225&&x<228))))assert(frame[y*410+x]==0x9215);
 s.battery_mode=Battery::Mode::Charging;s.battery_external=true;
 std::fill(frame.begin(),frame.end(),uint16_t(0x9215));footer(s);
 for(int y=472;y<486;y++)for(int x=188;x<216;x++)assert((frame[y*410+x]==0x1082)==(full[y*410+x]==0x1082));
 // A charging center contains only the filled bolt, never remnants of digits.
 for(int y=472;y<486;y++)for(int x=188;x<197;x++)assert(frame[y*410+x]!=0x1082);
 printf("%d layout cases: text stays in 50 px safe area, status confined to centered edge strips; all 32-byte ASCII names retained; insertion ranges and every deletion caret visible through 0..63 characters.\n",cases);
}
'''
with scratch_directory('layout-status',reserve_mib=64) as d:
 p=Path(d);(p/'check.cpp').write_text(prefix+body+suffix)
 subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-I',str(ROOT/'main'),str(p/'check.cpp'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True,env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1','ASAN_OPTIONS':'halt_on_error=1'})
