"""Generate tightly subsetted, one-bit Archivo and JetBrains Mono glyph masks."""
from pathlib import Path
import argparse
from PIL import Image, ImageDraw, ImageFont
p=argparse.ArgumentParser(); p.add_argument('font_dir',type=Path); p.add_argument('output',type=Path); a=p.parse_args()
chars=''.join(chr(i) for i in range(32,127))
sets=[(0,n,chars if n<=56 else '0123456789%:-. ') for n in [24,28,30,34,38,42,50,56,104,120,136,184]]+[(1,n,chars) for n in [15,16,17,20]]
header='''#pragma once
#include <cstdint>
struct Glyph { uint32_t offset; int16_t width, height, left, top, advance; uint8_t character; };
struct Font { const uint8_t* bitmap; const Glyph* glyphs; int count, size, family; };
extern const Font fonts[];
extern const int font_count;
'''
(a.output/'fonts.hpp').write_text(header)
out=['#include "fonts.hpp"']; names=[]
for family,size,subset in sets:
    f=ImageFont.truetype(str(a.font_dir/('Archivo.ttf' if family==0 else 'JetBrainsMono.ttf')),size)
    axes=f.get_variation_axes(); f.set_variation_by_axes([700 if x['name']==b'Weight' and family==0 else 500 if x['name']==b'Weight' else x['default'] for x in axes])
    # Inlining this in the glyph f-string below would nest single quotes, which requires Python 3.12.
    baseline=f.getbbox('0',anchor='la')[1]
    data=bytearray(); glyphs=[]
    for ch in subset:
        l,t,r,b=f.getbbox(ch,anchor='la'); w,h=r-l,b-t
        im=Image.new('L',(max(1,w),max(1,h)));ImageDraw.Draw(im).text((-l,-t),ch,font=f,fill=255,anchor='la')
        # Threshold masks preserve the requested four palette values at text edges.
        bits=[int(v>=128) for v in im.getdata()] if w*h else []
        off=len(data)
        for i in range(0,len(bits),8): data.append(sum(v<<(7-j) for j,v in enumerate(bits[i:i+8])))
        glyphs.append(f'{{{off},{w},{h},{l},{t-baseline},{round(f.getlength(ch))},{ord(ch)}}}')
    name=f'f{family}_{size}';names.append(f'{{{name}_bits,{name}_glyphs,{len(subset)},{size},{family}}}')
    out.append(f'static const uint8_t {name}_bits[]={{'+','.join(map(str,data))+'};')
    out.append(f'static const Glyph {name}_glyphs[]={{'+','.join(glyphs)+'};')
out.append('const Font fonts[]={'+','.join(names)+'};\nconst int font_count=sizeof(fonts)/sizeof(fonts[0]);')
(a.output/'fonts.cpp').write_text('\n'.join(out));print('Generated font source bytes:',(a.output/'fonts.cpp').stat().st_size)
