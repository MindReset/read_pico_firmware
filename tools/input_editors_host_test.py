"""中文：抽取实际编辑回调，验证改名、书名、资料卡与签名的中途编辑。
English: Extract actual editor callbacks for filename, book title, profile and signature insertion.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

def function(path, name):
    text = (root / path).read_text()
    tokens = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    scan = re.sub(tokens, lambda m: ' ' * len(m[0]) if m[0].startswith(('//', '/*')) else m[0], text)
    match = re.search(r'^static [^\n]+\b' + name + r'\([^;{}]*?\)\s*\{', scan, re.M)
    assert match, name
    start, at, depth, quote, escaped = match.start(), match.end(), 1, None, False
    while depth:
        c = scan[at]
        if quote:
            if escaped: escaped = False
            elif c == '\\': escaped = True
            elif c == quote: quote = None
        elif c in "\"'": quote = c
        elif c == '{': depth += 1
        elif c == '}': depth -= 1
        at += 1
    return text[start:at]

common = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ui_text_edit.h"
static char s_editor[121],s_editor_title[121],s_editor_ext[16],s_editor_pinyin[24],s_editor_notice[96],s_message[96];
static ui_text_edit_t s_editor_input;
static bool s_editor_chinese,s_editor_signature,s_editor_uppercase;
static size_t s_editor_candidate_page,s_editor_candidate_count;
static int s_view,s_page;
enum {FILE_VIEW_RENAME,EDIT,SETTINGS_TEXT_EDIT};
static struct {char name[128],path[288];bool is_dir;} s_selected,s_managed;
static uint8_t *s_editor_cover;
static uint8_t *load_cover_gray(const char *p,const char *n,const char *a,bool f,bool *pending){(void)p;(void)n;(void)a;(void)f;*pending=false;return NULL;}
static void copy_utf8(char *out,size_t cap,const char *s){snprintf(out,cap,"%s",s);}
static void copy_text(char *out,size_t cap,const char *s){snprintf(out,cap,"%s",s);}
static void editor_refresh(void){}
static void editor_refresh_candidates(void){}
static void profile_editor_refresh(void){}
static const char *app_settings_device_name(void){return "我的Pico";}
static const char *app_settings_status_signature(void){return "慢慢阅读";}
'''
scenes = [
    ('main/apps/app_files.c', 'editor_start', 's_editor', r' strcpy(s_selected.name,"你好世界.epub");editor_start();assert(!strcmp(s_editor_ext,".epub")&&!strcmp(s_editor,"你好世界")); s_selected.is_dir=true;strcpy(s_selected.name,"目录");editor_start();assert(!*s_editor_ext);'),
    ('main/apps/app_device_settings.c', 'profile_editor_open', 's_editor', r' profile_editor_open(false);assert(!strcmp(s_editor,"我的Pico"));profile_editor_open(true);assert(!strcmp(s_editor,"慢慢阅读"));'),
    ('main/apps/app_book.c', 'editor_start', 's_editor_title', r' strcpy(s_managed.name,"你好世界");editor_start();assert(!strcmp(s_editor_title,"你好世界"));'),
]
common += r"""
static ui_text_edit_t *test_bound;
static void ui_keyboard_begin(ui_text_edit_t *edit,bool ascii){assert(!ascii);test_bound=edit;}
"""
for path, name, buffer, lifecycle in scenes:
    unit=common+'\n'+function(path,name)+'\nint main(void){'+lifecycle+f'''
        assert(test_bound==&s_editor_input&&s_editor_input.text=={buffer});
        assert(s_editor_input.cursor==strlen({buffer})&&s_editor_input.capacity==sizeof({buffer}));
        ui_text_edit_place(&s_editor_input,3);assert(ui_text_edit_insert(test_bound,"ABC"));
        assert(ui_text_edit_backspace(test_bound));
        assert(strstr({buffer},"AB"));puts("input_editor: PASS ({path})");
    }}'''
    with tempfile.TemporaryDirectory() as tmp:
        c,exe=Path(tmp)/'test.c',Path(tmp)/'test';c.write_text(unit)
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',
                        '-fsanitize=address,undefined','-I',str(root/'main/ui'),str(c),
                        str(root/'main/ui/ui_text_edit.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
