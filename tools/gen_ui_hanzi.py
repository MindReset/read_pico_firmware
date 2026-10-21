#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：把内建黑体缺少的 GB2312 汉字生成分块压缩位图，避免整份字体驻留。
# English: Generate block-compressed missing GB2312 glyphs from the built-in face without retaining a whole font in RAM.
import argparse, ctypes, hashlib, struct, subprocess, zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE=24
BLOCK=32
RECORD=77

def main():
    from fontTools.ttLib import TTFont
    from fontTools.varLib.instancer import instantiateVariableFont
    parser=argparse.ArgumentParser();parser.add_argument('--src',type=Path,default=ROOT/'sdcard/fonts/Hei.ttf');args=parser.parse_args()
    work=ROOT/'build/ui-hanzi-generator';work.mkdir(parents=True,exist_ok=True)
    dest=work/'medium.ttf';stamp=work/'source.sha256';digest=hashlib.sha256(args.src.read_bytes()).hexdigest()
    if not dest.is_file() or not stamp.is_file() or stamp.read_text()!=digest:
        font=TTFont(args.src);font=instantiateVariableFont(font,{'wght':500},inplace=True);font.save(dest);font.close();stamp.write_text(digest)
    covered=set(TTFont(ROOT/'main/assets/builtin.ttf').getBestCmap());wanted=set()
    for hi in range(0xb0,0xf8):
        for lo in range(0xa1,0xff):
            try:wanted.add(ord(bytes([hi,lo]).decode('gb2312')))
            except UnicodeDecodeError:pass
    chars=sorted(wanted-covered)
    helper=work/'glyph.c';helper.write_text('''#include <stdint.h>
#include <string.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
static stbtt_fontinfo font;
int init(const unsigned char *data){return stbtt_InitFont(&font,data,stbtt_GetFontOffsetForIndex(data,0));}
int render(uint32_t cp,uint8_t *out){
 if(!stbtt_FindGlyphIndex(&font,cp))return 0;
 float s=stbtt_ScaleForPixelHeight(&font,24);int x0,y0,x1,y1,advance,left;
 stbtt_GetCodepointBitmapBox(&font,cp,s,s,&x0,&y0,&x1,&y1);
 stbtt_GetCodepointHMetrics(&font,cp,&advance,&left);
 int w=x1-x0,h=y1-y0;if(w<1||h<1||w>24||h>24)return 0;
 uint8_t pixels[576]={0};stbtt_MakeCodepointBitmap(&font,pixels,w,h,w,s,s,cp);
 memset(out,0,77);out[0]=(uint8_t)x0;out[1]=(uint8_t)y0;out[2]=w;out[3]=h;out[4]=(int)(advance*s+0.5f);
 for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(pixels[y*w+x]>=96)out[5+(y*24+x)/8]|=1u<<((y*24+x)%8);
 return 1;
}
''')
    libpath=work/'glyph.dylib';subprocess.run(['cc','-shared','-fPIC','-O2','-I'+str(ROOT/'main/font'),str(helper),'-o',str(libpath)],check=True)
    lib=ctypes.CDLL(str(libpath));buffer=ctypes.create_string_buffer(dest.read_bytes());assert lib.init(buffer)
    record=(ctypes.c_uint8*RECORD)();raw=[]
    for cp in chars:
        assert lib.render(cp,record),f'missing source glyph {cp:x}'
        raw.append(bytes(record))
    offsets=[0];blocks=b''
    for start in range(0,len(raw),BLOCK):
        blocks+=zlib.compress(b''.join(raw[start:start+BLOCK]),9);offsets.append(len(blocks))
    data=struct.pack('<4sIII',b'PIF1',len(chars),BASE,BLOCK)
    data+=struct.pack('<'+'H'*len(chars),*chars)+struct.pack('<'+'I'*len(offsets),*offsets)+blocks
    out=ROOT/'main/assets/ui-hanzi.bin';out.write_bytes(data)
    print(f'UI black font: {len(chars)} additional GB2312 characters, {len(data)} bytes, at most {BLOCK*RECORD} bytes decoded per block')

if __name__=='__main__':main()
