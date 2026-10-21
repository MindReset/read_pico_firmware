/* SPDX-License-Identifier: Apache-2.0
 * 中文：摘句沿用图书缓存原子外壳，渲染不读卡；最多检查32KiB已解析正文。
 * English: Sentence caches reuse atomic book-cache files; rendering never reads media, and extraction examines at most 32 KiB of parsed text.
 */
#include "book_excerpt.h"
#include "book_index_cache.h"
#include <string.h>
static uint32_t checksum(const char *s) {
    uint32_t h=2166136261u;
    for (; *s; ++s) h=(h^(uint8_t)*s)*16777619u;
    return h;
}
typedef struct { uint32_t checksum; char text[BOOK_EXCERPT_CAP]; } excerpt_t;
bool book_excerpt_load(const char *source, char out[BOOK_EXCERPT_CAP]) {
    if (!out) return false;
    out[0]=0;
    char path[160]; book_index_cache_header_t key;
    if (!book_index_cache_prepare(source,"quote",1,path,sizeof(path),&key)) return false;
    FILE *file=book_index_cache_open_read(path,&key);
    if (!file) return false;
    excerpt_t value;
    bool ok=fread(&value,1,sizeof(value),file)==sizeof(value) && fgetc(file)==EOF;
    fclose(file);
    ok=ok && strnlen(value.text,sizeof(value.text))<sizeof(value.text) && value.text[0] && value.checksum==checksum(value.text);
    if (ok) memcpy(out,value.text,sizeof(value.text));
    return ok;
}
static unsigned decode(const char *s, size_t remain, uint32_t *cp) {
    unsigned c=(uint8_t)s[0], n=c<128?1:c>=0xc2&&c<=0xdf?2:c<=0xef&&c>=0xe0?3:c>=0xf0&&c<=0xf4?4:0;
    if (!n || n>remain) return 0;
    *cp=c & (n==1?127:n==2?31:n==3?15:7);
    for (unsigned i=1;i<n;++i) { if (((uint8_t)s[i]&0xc0)!=0x80) return 0; *cp=(*cp<<6)|((uint8_t)s[i]&63); }
    if ((n==2&&*cp<128)||(n==3&&*cp<0x800)||(n==4&&*cp<0x10000)||(*cp>=0xd800&&*cp<=0xdfff)||*cp>0x10ffff) return 0;
    return n;
}
static bool sentence(const char *s,size_t len,char *out) {
    size_t start=0; unsigned units=0,chars=0;
    for (size_t i=0;i<len;) {
        uint32_t cp; unsigned n=decode(s+i,len-i,&cp);
        if (!n) { ++i; start=i; units=chars=0; continue; }
        if (cp=='\n'||cp=='\r') { i+=n; start=i; units=chars=0; continue; }
        if (!chars && (cp<=32||cp==0x3000||cp==0x201c||cp==0x2018)) { i+=n; start=i; continue; }
        units+=cp<128?1:2; ++chars;
        bool end=cp==0x3002||cp==0xff01||cp==0xff1f||cp=='!'||cp=='?' || (cp=='.' && (i+n==len||s[i+n]==' '));
        i+=n;
        if (end) {
            if (chars>=8 && units<=50 && i-start<BOOK_EXCERPT_CAP) { memcpy(out,s+start,i-start); out[i-start]=0; return true; }
            start=i; units=chars=0;
        }
    }
    return false;
}
void book_excerpt_offer(const char *source,const html_text_t *text) {
    if (!source || !text || !text->utf8 || !text->len) return;
    excerpt_t value={0};
    if (book_excerpt_load(source,value.text)) return;
    size_t budget=32768;
    if (!text->count) sentence(text->utf8,text->len<budget?text->len:budget,value.text);
    for (size_t i=0; i<text->count&&budget&&!value.text[0]; ++i) {
        const blk_t *b=&text->blocks[i];
        if (b->heading||b->auxiliary||b->linked||b->image>=0||b->offset>=text->len) continue;
        size_t n=b->len;
        if (n>text->len-b->offset) n=text->len-b->offset;
        if (n>budget) n=budget;
        sentence(text->utf8+b->offset,n,value.text); budget-=n;
    }
    if (!value.text[0]) return;
    value.checksum=checksum(value.text);
    char path[160],temp[176]; book_index_cache_header_t key;
    if (!book_index_cache_prepare(source,"quote",1,path,sizeof(path),&key)) return;
    FILE *file=book_index_cache_open_write(path,&key,temp,sizeof(temp));
    if (file) book_index_cache_finish_write(file,temp,path,fwrite(&value,1,sizeof(value),file)==sizeof(value));
}
