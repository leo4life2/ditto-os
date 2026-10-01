"""Compile the actual tile submission code against a delayed-transfer panel mock."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'main/board.cpp').read_text()
body=s[s.index('static bool tile_pending;'):s.index('void board_clear()')]
prefix=r"""
#define IRAM_ATTR
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "display_window.h"
constexpr int SCREEN_W=410,SCREEN_H=502,DMA_PIXELS=2*SCREEN_W*20;
std::vector<uint16_t> backing(SCREEN_W*SCREEN_H),physical(backing.size()),buffers(DMA_PIXELS);
uint16_t *frame=backing.data(),*dma=buffers.data();
int lcd_mutex=1,transfer=2,state_mutex=3,panel=0;
constexpr int portMAX_DELAY=0,pdTRUE=1,ESP_OK=0;
using esp_err_t=int;
struct {int display_errors=0;float diff_ms=0,copy_ms=0,dma_wait_ms=0,submit_ms=0;} state;
int64_t esp_timer_get_time(){return 0;}
struct Pending {int x,y,w,h;const uint16_t* p=nullptr;} pending;
int transfers=0;
int pdMS_TO_TICKS(int n){return n;}
int xSemaphoreTake(int sem,int){if(sem==transfer){assert(pending.p);for(int y=0;y<pending.h;y++)for(int x=0;x<pending.w;x++)physical[(pending.y+y)*SCREEN_W+pending.x+x]=__builtin_bswap16(pending.p[y*pending.w+x]);pending.p=nullptr;}return 1;}
void xSemaphoreGive(int){}
int esp_lcd_panel_draw_bitmap(int,int x,int y,int r,int b,const uint16_t* p){assert(!pending.p);assert(((x|y|r|b)&1)==0);assert((r-x)*(b-y)<=DMA_PIXELS/2);pending={x,y,r-x,b-y,p};transfers++;return 0;}
"""
suffix=r"""
int main(){std::vector<uint16_t> expected(SCREEN_W*SCREEN_H);for(int test=0;test<100;test++){if(test==0){for(int i=0;i<(int)expected.size();i++)expected[i]=i*71;}else if(test%3==0){for(int y=51;y<451;y++)for(int x=63;x<347;x++)expected[y*SCREEN_W+x]=(x+y+test)*37;}else{for(int i=0;i<80;i++)expected[(test*1837+i*901)%expected.size()]^=0xf891;}
if(test>=10){std::fill(expected.begin(),expected.end(),0);if(test%10){int l=(test*13)%300,t=(test*17)%360;for(int y=t;y<std::min(t+140,SCREEN_H);y++)for(int x=l;x<std::min(l+109,SCREEN_W);x++)expected[y*SCREEN_W+x]=(test*101+x+y)|1;}}
std::vector<std::pair<int,int>> bands;for(int y=0;y<96;y+=16)bands.push_back({y,16});for(int y=40;y<400;y+=20){int first=std::max(96,y+42),end=std::min(424,y+62);if(first<end)bands.push_back({first,end-first});}for(int y=424;y<SCREEN_H;y+=16)bands.push_back({y,std::min(16,SCREEN_H-y)});if(test%2)std::reverse(bands.begin(),bands.end());unsigned sequence=0; std::vector<uint16_t> worker(410*20);bool capturing = test%3 == 0 || test == 99; board_present_begin(capturing);for(auto band:bands){int y=band.first,h=band.second;auto p=board_tile_buffer(sequence++&1);if(test%2){std::copy_n(expected.data()+y*SCREEN_W,h*SCREEN_W,worker.data());board_present_tile(y,h,worker.data(),test%3!=0,p);std::fill(worker.begin(),worker.end(),0xabcd);}else{std::copy_n(expected.data()+y*SCREEN_W,h*SCREEN_W,p);board_present_tile(y,h,p,test%3!=0,nullptr);}}board_present_end();if(capturing)assert(backing==expected);assert(physical==expected);assert(!pending.p);}printf("100 complete native frames in alternating tile orders matched capture shadow and delayed-DMA panel; %d aligned transfers; private worker tiles and in-place static tiles; no reused in-flight buffers.\n",transfers);}
"""
with tempfile.TemporaryDirectory(prefix='scout-transport-') as d:
    cpp=Path(d)/'test.cpp';binary=Path(d)/'test'
    cpp.write_text(prefix+body+suffix)
    subprocess.run(['c++','-std=c++17','-O2','-I',str(ROOT/'main'),str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
