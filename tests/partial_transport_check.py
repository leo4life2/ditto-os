"""Exercise production immutable-buffer/partial-patch code with deferred DMA."""
from pathlib import Path
import subprocess,sys,re,os
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from storage_hygiene import scratch_directory
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'main/board.cpp').read_text();body=s[s.index('uint16_t *board_framebuffer()'):s.index('static void wait_until(')]
prefix=r'''
#define IRAM_ATTR
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include "display_window.h"
#include "scene_layout.h"
#include "scene_damage.h"
void board_present_tile(int,int,uint16_t*,bool,uint16_t*,const SceneDamageBounds* = nullptr);
constexpr int SCREEN_W=410,SCREEN_H=502,DMA_PIXELS=2*SCREEN_W*12,SCENE_TILE_ROWS=18,COMPOSE_PIXELS=2*SCREEN_W*18;
std::vector<uint16_t> first(SCREEN_W*SCREEN_H),second(first.size()),physical(first.size()),buffers(COMPOSE_PIXELS);
uint16_t *frame=first.data(),*ready_frame=second.data(),*compose_tiles=buffers.data();
uint32_t ready_unmerged_strips=0,ready_unmerged_pixels=0;
DisplayWindow dirty[64],ready_dirty[64];int dirty_count=0,ready_count=0;bool static_rows_changed=false;
constexpr int present_free=1,present_ready=2,transfer=3,state_mutex=4,present_collect=5,portMAX_DELAY=0,pdTRUE=1;
struct {int display_errors=0;float diff_ms=0,copy_ms=0,dma_wait_ms=0,submit_ms=0;} state;
int64_t esp_timer_get_time(){return 0;}
int pdMS_TO_TICKS(int n){return n;}
int xSemaphoreTake(int,int);void xSemaphoreGive(int);
'''
# Use the actual firmware capacities; a hard-coded 18-row test hid the old
# 16-row sleeping-clock / 12-row compositor crash.
tile=int(re.search(r'SCENE_TILE_ROWS = (\d+)',(ROOT/'main/scout.h').read_text()).group(1))
compose=s[s.index('static constexpr int COMPOSE_ROWS'):s.index('static esp_codec_dev_handle_t')]
prefix=prefix.replace('SCENE_TILE_ROWS=18,COMPOSE_PIXELS=2*SCREEN_W*18;',f'SCENE_TILE_ROWS={tile};\n'+compose)
suffix=r'''
bool pending=false;std::vector<uint16_t> immutable;uint64_t pixels_sent=0;
void finish(){if(!pending)return;assert(std::equal(immutable.begin(),immutable.end(),ready_frame));for(int i=0;i<ready_count;i++){auto a=ready_dirty[i];assert(!((a.x|a.y|a.width|a.height)&1));for(int y=a.y;y<a.y+a.height;y++)std::copy_n(ready_frame+y*410+a.x,a.width,physical.data()+y*410+a.x);pixels_sent+=a.width*a.height;}pending=false;}
std::mutex collection;
int xSemaphoreTake(int sem,int){if(sem==present_collect)collection.lock();if(sem==present_free)finish();return 1;}
void xSemaphoreGive(int sem){if(sem==present_collect)collection.unlock();if(sem==present_ready){assert(!pending);pending=true;immutable.assign(ready_frame,ready_frame+410*502);}}
int main(){for(int y=0;y<502;y++){frame_bounds[y]=ready_bounds[y]={205,0};}std::vector<uint16_t> expected(410*502),patch(76*104);for(int cycle=0;cycle<20;cycle++){
 for(int pass=0;pass<2;pass++){for(int y=0;y<502;y++)for(int x=0;x<410;x++)expected[y*410+x]=(cycle%7!=3 && (cycle%4==0 || (x>(cycle*47+pass*83)%250 && x<(cycle*47+pass*83)%250+120 && y>(cycle*37)%250 && y<(cycle*37)%250+180))) ? ((x*7+y*17+cycle*101+pass*53)|1) : 0;board_present_begin(false,false);auto submit=[&](int parity){for(int y=parity*16;y<502;y+=32)board_present_tile(y,std::min(16,502-y),expected.data()+410*y,true,nullptr);};std::thread other([&]{submit(1);});submit(0);other.join();board_present_end();assert(std::equal(expected.begin(),expected.end(),board_framebuffer()));if(pass==1)finish();}assert(physical==expected);
 for(int i=0;i<20;i++){board_present_begin(i%5==0,true);for(int y=0;y<104;y++)for(int x=0;x<76;x++){int index=(120+y)*410+268+x;expected[index]=(x+y+i+cycle)*109;patch[y*76+x]=expected[index];}board_present_patch(268,120,76,104,patch.data());board_present_end();finish();assert(physical==expected);assert(std::equal(expected.begin(),expected.end(),board_framebuffer()));}
 // Minute clock update and body patch in the same transaction.
 board_present_begin(true,true);for(int y=0;y<144;y+=16){for(int j=0;j<16*410;j++)expected[y*410+j]^=0x3aa1;board_present_tile(y,16,expected.data()+410*y,false,nullptr);}board_present_end();finish();assert(physical==expected);
 }
 SceneDamageBounds preceding,current;
 for(int frame_index=0;frame_index<120;frame_index++) {
  // Opaque moving patches over a nonblack, stationary full-screen field.
  // Repeated frames must not cause uploads or leave the spare two frames old.
  if(frame_index%3!=1) {
   for(int y=0;y<502;y++)for(int x=0;x<410;x++)expected[y*410+x]=uint16_t((x*97+y*13)|1);
   int px=(frame_index*29)%390,py=(frame_index*37)%482;
   current={};current.point(px,py,0);current.point(px+19,py+19,0);
   for(int y=py;y<py+20;y++)for(int x=px;x<px+20;x++)expected[y*410+x]=frame_index%5?0:0xffff;
  }
  SceneDamageBounds damage=current;damage.unite(preceding);preceding=current;
  uint64_t before=pixels_sent;
  board_present_begin(false,false);
  for(int y=0;y<502;y+=18){int h=std::min(18,502-y);if(frame_index && !board_scene_rows_needed(y,h,damage))continue;board_present_tile(y,h,expected.data()+410*y,true,nullptr,frame_index?&damage:nullptr);}
  board_present_end();finish();assert(physical==expected);assert(std::equal(expected.begin(),expected.end(),board_framebuffer()));
  // Stationary repeats may conservatively upload, but must remain exact.
 }
 printf("160 full/repeated frames, 400 small patches, 20 minute updates and deferred immutable DMA matched every physical pixel; %llu pixels transferred.\n",(unsigned long long)pixels_sent);}
'''
with scratch_directory('partial-transport',reserve_mib=24) as d:
 p=Path(d);(p/'test.cpp').write_text(prefix+body+suffix)
 subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-pthread','-I',str(ROOT/'main'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'}
 subprocess.run([str(p/'test')],check=True,env=env)
 # The rejected coupling must still reproduce the specific clock-band failure.
 if tile<16:
  bad=(prefix+body+suffix).replace('std::max(SCENE_TILE_ROWS,18)','SCENE_TILE_ROWS')
  (p/'bad.cpp').write_text(bad)
  subprocess.run(['c++','-std=c++17','-O2','-pthread','-I',str(ROOT/'main'),str(p/'bad.cpp'),'-o',str(p/'bad')],check=True)
  failed=subprocess.run([str(p/'bad')],capture_output=True,text=True)
  assert failed.returncode!=0 and 'COMPOSE_PIXELS/2' in failed.stderr,failed.stderr
  print('PASS: former 12-row composition reproduces clock-strip assertion; independent composition capacity fixes it')
