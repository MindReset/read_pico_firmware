#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""驻留字宽测量回归，不允许访问字形轮廓。/ Resident advance regression without outline access."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'main/font/ttf_font.c').read_text(encoding='utf-8')

def function(name):
    match = re.search(r'^static [^\n]+\b' + name + r'\([^\n]*\) \{', SOURCE, re.M)
    assert match
    at, depth = match.end(), 1
    while depth:
        depth += (SOURCE[at] == '{') - (SOURCE[at] == '}')
        at += 1
    return SOURCE[match.start():at]

unit = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
static stbtt_fontinfo font_info;
'''
unit += function('decode_utf8') + '\n' + function('measure_width')
unit += r'''
int main(int argc,char** argv) {
    for(int a=1;a<argc;++a) {
        FILE* f=fopen(argv[a],"rb");assert(f);fseek(f,0,SEEK_END);long len=ftell(f);rewind(f);
        unsigned char* data=malloc((size_t)len);assert(data);assert(fread(data,1,(size_t)len,f)==(size_t)len);fclose(f);
        assert(stbtt_InitFont(&font_info,data,0));
        const char* strings[]={"","Wi 0123","甲乙丙丁 世界 小纸 Pico","继续上次阅读？"};
        for(int px=12;px<=80;px+=4) for(unsigned i=0;i<sizeof(strings)/sizeof(strings[0]);++i) {
            int expected=0;float scale=stbtt_ScaleForPixelHeight(&font_info,(float)px);
            const char* at=strings[i];
            while(*at){int advance,lsb;stbtt_GetCodepointHMetrics(&font_info,(int)decode_utf8(&at),&advance,&lsb);expected+=(int)lroundf(advance*scale);}
            int glyf=font_info.glyf,loca=font_info.loca;
            font_info.glyf=font_info.loca=INT_MAX;
            assert(measure_width(px,strings[i])==expected);
            font_info.glyf=glyf;font_info.loca=loca;
        }
        free(data);
    }
    puts("resident font metrics: builtin/full fonts, mixed text and 18 sizes pass without outline access");
}
'''
with tempfile.TemporaryDirectory(prefix='font-metrics-') as temp:
    path = Path(temp)
    (path / 'test.c').write_text(unit, encoding='utf-8')
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Imain/font',str(path / 'test.c'),'-lm','-o',str(path / 'test')],cwd=ROOT,check=True)
    subprocess.run([str(path / 'test'),str(ROOT / 'main/assets/builtin.ttf'),str(ROOT / 'sdcard/fonts/ChillDuanSansVF.ttf')],check=True)
