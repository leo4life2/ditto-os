"""Bake native-resolution Outfit UI and IBM Plex Mono password masks."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
ROOT = Path(__file__).resolve().parents[1]
source = ROOT / 'assets/fonts/IBMPlexMono-Medium.ttf'
out = ['#pragma once', '#include <cstdint>', 'struct Glyph {uint32_t offset; uint8_t w,h; int8_t x,y; uint8_t advance;};', 'struct Font {const Glyph* glyphs; const uint8_t* data; int height;};']
for name,size,weight in [('14',16,500),('20',22,600),('28',28,700),('64',76,750),('88',88,750),('mono',20,None)]:
    font = ImageFont.truetype(str(source if weight is None else ROOT/'assets/fonts/Outfit-Variable.ttf'), size)
    if weight is not None: font.set_variation_by_axes([weight])
    data, glyphs = [], []
    for c in range(32,127):
        box = font.getbbox(chr(c)); x,y,x1,y1=box; w,h=x1-x,y1-y
        im=Image.new('L',(max(1,w),max(1,h)))
        ImageDraw.Draw(im).text((-x,-y),chr(c),font=font,fill=255)
        offset=len(data); data.extend(im.tobytes() if w*h else [])
        glyphs.append('{%d,%d,%d,%d,%d,%d}'%(offset,w,h,x,y,round(font.getlength(chr(c)))))
    out += [f'inline const uint8_t font_data_{name}[]={{'+','.join(map(str,data))+'};', f'inline const Glyph font_glyphs_{name}[]={{'+','.join(glyphs)+'};', f'inline const Font font_{name}={{font_glyphs_{name},font_data_{name},{size+4}}};']
(ROOT/'main/fonts.h').write_text('\n'.join(out)+'\n')
print('Prepared native UI and password font masks')
