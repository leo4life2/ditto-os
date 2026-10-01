"""Compare clipped glyph rendering with the original per-pixel operation."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'main/ui.cpp').read_text();body=s[s.index('static uint16_t blend'):s.index('static void header')]
prefix=r"""
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "fonts.h"
constexpr int SCREEN_W=410,SCREEN_H=502,SAFE=50;
static uint16_t *canvas;
static int top,rows,ui_alpha;
static int canvas_left=0,canvas_stride=410;
"""
suffix=r"""
static void reference(const Font &f,const char *s,int x,int y,uint16_t c){for(;*s;s++){unsigned char ch=*s;if(ch<32||ch>126)ch='?';const auto&g=f.glyphs[ch-32];for(int yy=0;yy<g.h;yy++)for(int xx=0;xx<g.w;xx++)pixel(x+g.x+xx,y+g.y+yy,c,f.data[g.offset+yy*g.w+xx]);x+=g.advance;}}
int main(){std::vector<uint16_t> a(410*502),b(a.size());for(int test=0;test<12;test++){for(int i=0;i<(int)a.size();i++)a[i]=b[i]=i*317;ui_alpha=test%3==0?255:test%3==1?113:0;for(int band=0;band<502;band+=16){top=band;rows=std::min(16,502-band);for(int which=0;which<2;which++){canvas=(which?a:b).data()+band*410;auto fn=which?text:reference;fn(font_88,"12:37",30+test*7,21,0xff9c);fn(font_20,"A little brighter.",-10+test*9,407,0xfb4c);fn(font_mono,"example345-long-password",test*3,365,0xffff);fn(font_14,"Voice not connected",140,433,0x9cf3);}}assert(a==b);}puts("12 tiled layouts match reference glyph blending and 50-pixel clipping exactly.");}
"""
with tempfile.TemporaryDirectory(prefix='scout-text-') as d:
    cpp=Path(d)/'test.cpp';exe=Path(d)/'test';cpp.write_text(prefix+body+suffix)
    subprocess.run(['c++','-std=c++17','-O2','-I',str(ROOT/'main'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
