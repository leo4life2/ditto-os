"""Exercise native input -> expanded paired windows -> panel byte order."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'main/board.cpp').read_text()
body=s[s.index('void board_blit('):s.index('uint16_t *board_framebuffer()')]
prefix=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "display_window.h"
constexpr int SCREEN_W=410,SCREEN_H=502,DMA_PIXELS=2*410*12;
constexpr int lcd_mutex=1,transfer=2,state_mutex=3,portMAX_DELAY=0,pdTRUE=1,ESP_OK=0;
using esp_err_t=int;
std::vector<uint16_t> shadow(410*502),stage(DMA_PIXELS),physical(shadow.size());
uint16_t *frame=shadow.data(),*dma=stage.data();int panel=0;
struct {int display_errors=0;}state;
void invalidate_bounds(int,int){}
int xSemaphoreTake(int,int){return 1;}void xSemaphoreGive(int){}int pdMS_TO_TICKS(int n){return n;}
int esp_lcd_panel_draw_bitmap(int,int x,int y,int right,int bottom,const void *data){
 assert(!((x|y|right|bottom)&1));auto bytes=static_cast<const uint8_t*>(data);
 for(int yy=y;yy<bottom;yy++)for(int xx=x;xx<right;xx++){
  int i=((yy-y)*(right-x)+xx-x)*2;physical[yy*410+xx]=(uint16_t(bytes[i])<<8)|bytes[i+1];
 }return ESP_OK;
}
'''
suffix=r'''
int main(){uint32_t random=0x981da71;auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
 for(size_t i=0;i<shadow.size();i++){physical[i]=uint16_t(next());shadow[i]=physical[i];}
 auto expected=physical;
 for(int pass=0;pass<1000;pass++){
  int x=next()%410,y=next()%502,w=1+next()%(410-x),h=1+next()%(502-y),stride=w+3;
  std::vector<uint16_t> source(stride*h);
  for(int yy=0;yy<h;yy++)for(int xx=0;xx<stride;xx++)source[yy*stride+xx]=uint16_t(next());
  for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++)expected[(y+yy)*410+x+xx]=source[yy*stride+xx];
  board_blit(x,y,w,h,source.data(),stride);assert(physical==expected);
  for(size_t i=0;i<shadow.size();i++)assert(shadow[i]==expected[i]);
 }
 puts("1000 native blits preserved every panel pixel, paired borders, odd windows, strides and native shadow storage.");
}
'''
with tempfile.TemporaryDirectory(prefix='scout-wire-blit-') as d:
 p=Path(d);(p/'test.cpp').write_text(prefix+body+suffix)
 subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-I',str(ROOT/'main'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
