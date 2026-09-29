/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：将常见 EPUB 插图变为适合墨水屏的白底灰度图。
 * English: Convert common EPUB illustrations to white-backed grayscale for the panel.
 * 冻结：只在书源加载阶段串行调用；解码器分配总量有界，不在 render 中解码。
 * Frozen: Serialize calls during source loading; bound total decoder allocation and never decode inside render.
 */
#include "book_image.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>
#include "vendor/tjpgd.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define IMAGE_HEAP_MAX (4u * 1024u * 1024u)
#define PSRAM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
typedef union { size_t size; max_align_t align; } allocation_t;
static size_t allocated;

static void* image_alloc(size_t n) {
    if (n > SIZE_MAX - sizeof(allocation_t) || n + sizeof(allocation_t) > IMAGE_HEAP_MAX - allocated) return NULL;
    allocation_t* p = heap_caps_malloc(sizeof(*p) + n, PSRAM_CAPS);
    if (!p) return NULL;
    p->size = n; allocated += n + sizeof(*p);
    return p + 1;
}
static void image_free(void* ptr) {
    if (!ptr) return;
    allocation_t* p = (allocation_t*)ptr - 1;
    allocated -= p->size + sizeof(*p);
    free(p);
}
static void* image_realloc(void* ptr, size_t n) {
    if (!ptr) return image_alloc(n);
    allocation_t* old = (allocation_t*)ptr - 1;
    size_t prior = old->size;
    if (n > SIZE_MAX - sizeof(*old) || n > IMAGE_HEAP_MAX - (allocated - prior)) return NULL;
    allocation_t* p = heap_caps_realloc(old, sizeof(*old) + n, PSRAM_CAPS);
    if (!p) return NULL;
    p->size = n; allocated = allocated - prior + n;
    return p + 1;
}

#define STBI_MALLOC(n) image_alloc(n)
#define STBI_REALLOC(p,n) image_realloc(p,n)
#define STBI_FREE(p) image_free(p)
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 4096
#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb_image.h"

static uint32_t png_u32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static unsigned png_u16(const uint8_t* p) { return (unsigned)p[0] << 8 | p[1]; }
static uint8_t png_paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (uint8_t)(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

// 常见8位非交错PNG在解压缓冲内逐行还原，避免再分配一份完整RGB图。
// Unfilter common 8-bit noninterlaced PNG in place, avoiding a second full RGB image allocation.
static bool png_decode(const uint8_t* data, size_t len, size_t budget,
                       uint8_t** pixels, uint16_t* width, uint16_t* height) {
    if (len < 33 || memcmp(data,"\x89PNG\r\n\x1a\n",8) || png_u32(data+8)!=13 || memcmp(data+12,"IHDR",4)) return false;
    unsigned w=png_u32(data+16), h=png_u32(data+20), type=data[25];
    unsigned channels=type==0 ? 1 : type==2 ? 3 : type==3 ? 1 : type==4 ? 2 : type==6 ? 4 : 0;
    if (!w || !h || w>4096 || h>4096 || (uint64_t)w*h>1024u*1024u || !channels ||
        data[24]!=8 || data[26] || data[27] || data[28]) return false;
    const uint8_t *palette=NULL,*trans=NULL; size_t palette_n=0,trans_n=0,packed_n=0;
    bool idat=false,ended=false;
    for (size_t at=33;at<len;) {
        if (len-at<12) return false;
        size_t n=png_u32(data+at);
        if (n>len-at-12) return false;
        const uint8_t* tag=data+at+4;
        if (!memcmp(tag,"IDAT",4)) { packed_n+=n; idat=true; }
        else if (!memcmp(tag,"PLTE",4)) {
            if (idat || palette || !n || n%3 || n>768) return false;
            palette=data+at+8;palette_n=n/3;
        } else if (!memcmp(tag,"tRNS",4)) {
            if (idat || trans || (type==0 ? n!=2 : type==2 ? n!=6 : type==3 ? !n || n>256 : true)) return false;
            trans=data+at+8;trans_n=n;
        } else if (!memcmp(tag,"IEND",4)) { if(n)return false; ended=true;break; }
        else if (!(tag[0]&32)) return false;
        at+=n+12;
    }
    if (!ended || !packed_n || (type==3 && (!palette || trans_n>palette_n))) return false;
    unsigned dw=w,dh=h;
    if (dw>648) { dh=dh*648/dw;dw=648; }
    if (dh>1000) { dw=dw*1000/dh;dh=1000; }
    if(!dw)dw=1;
    if(!dh)dh=1;
    if((size_t)dw*dh>budget)return false;
    size_t row_n=(size_t)w*channels, raw_n=(row_n+1)*h;
    uint8_t* packed=image_alloc(packed_n);
    uint8_t* raw=image_alloc(raw_n);
    uint8_t* out=NULL;
    bool ok=false;
    if(!packed || !raw)goto done;
    size_t pos=0;
    for(size_t at=33;at+12<=len;) {
        size_t n=png_u32(data+at);
        if(!memcmp(data+at+4,"IDAT",4)){memcpy(packed+pos,data+at+8,n);pos+=n;}
        if(!memcmp(data+at+4,"IEND",4))break;
        at+=n+12;
    }
    if(stbi_zlib_decode_buffer((char*)raw,(int)raw_n,(const char*)packed,(int)packed_n)!=(int)raw_n)goto done;
    image_free(packed);packed=NULL;
    out=heap_caps_malloc((size_t)dw*dh,PSRAM_CAPS);
    if(!out)goto done;
    unsigned dy=0;
    for(unsigned y=0;y<h;++y) {
        uint8_t* row=raw+(row_n+1)*y+1;
        const uint8_t* prev=y ? row-row_n-1 : NULL;
        unsigned filter=row[-1];
        if(filter>4)goto done;
        for(size_t x=0;x<row_n;++x) {
            unsigned a=x>=channels ? row[x-channels] : 0,b=prev ? prev[x] : 0,c=prev && x>=channels ? prev[x-channels] : 0;
            unsigned add=filter==0 ? 0 : filter==1 ? a : filter==2 ? b : filter==3 ? (a+b)/2 : png_paeth(a,b,c);
            row[x]=(uint8_t)(row[x]+add);
        }
        if(dy>=dh || (uint64_t)dy*h/dh!=y)continue;
        for(unsigned x=0;x<dw;++x) {
            const uint8_t* p=row+(size_t)((uint64_t)x*w/dw)*channels;
            unsigned gray=0,alpha=255;
            if(type==0 || type==4) {
                gray=p[0];
                if(type==4)alpha=p[1];
                else if(trans && p[0]==png_u16(trans))alpha=0;
            } else if(type==3) {
                unsigned index=p[0];
                if(index>=palette_n)goto done;
                const uint8_t* color=palette+index*3;
                gray=(77u*color[0]+150u*color[1]+29u*color[2])>>8;
                if(index<trans_n)alpha=trans[index];
            } else {
                gray=(77u*p[0]+150u*p[1]+29u*p[2])>>8;
                if(type==6)alpha=p[3];
                else if(trans && p[0]==png_u16(trans) && p[1]==png_u16(trans+2) && p[2]==png_u16(trans+4))alpha=0;
            }
            out[(size_t)dy*dw+x]=(uint8_t)((gray*alpha+255u*(255u-alpha)+127u)/255u);
        }
        ++dy;
#ifdef ESP_PLATFORM
        if(!(y%32))vTaskDelay(1);
#endif
    }
    if(dy!=dh)goto done;
    *pixels=out;*width=(uint16_t)dw;*height=(uint16_t)dh;out=NULL;ok=true;
done:
    image_free(packed);image_free(raw);free(out);return ok;
}

typedef struct {
    const uint8_t* data;
    size_t len, at;
    uint8_t* out;
    unsigned width, height, scaled_width, scaled_height, blocks;
} jpeg_io_t;

static size_t jpeg_read(JDEC* jd, uint8_t* out, size_t count) {
    jpeg_io_t* io = jd->device;
    if (count > io->len - io->at) count = io->len - io->at;
    if (out) memcpy(out, io->data + io->at, count);
    io->at += count;
    return count;
}
static int jpeg_write(JDEC* jd, void* bitmap, JRECT* r) {
    jpeg_io_t* io = jd->device;
    if (r->right >= io->scaled_width || r->bottom >= io->scaled_height ||
        r->left > r->right || r->top > r->bottom) return 0;
    unsigned x0 = ((unsigned)r->left * io->width + io->scaled_width - 1) / io->scaled_width;
    unsigned x1 = ((unsigned)(r->right + 1) * io->width + io->scaled_width - 1) / io->scaled_width;
    unsigned y0 = ((unsigned)r->top * io->height + io->scaled_height - 1) / io->scaled_height;
    unsigned y1 = ((unsigned)(r->bottom + 1) * io->height + io->scaled_height - 1) / io->scaled_height;
    const uint8_t* rgb = bitmap;
    for (unsigned y = y0; y < y1; ++y) for (unsigned x = x0; x < x1; ++x) {
        unsigned sx = x * io->scaled_width / io->width - r->left;
        unsigned sy = y * io->scaled_height / io->height - r->top;
        const uint8_t* color = rgb + (sy * (r->right - r->left + 1) + sx) * 3;
        io->out[(size_t)y * io->width + x] = (uint8_t)((77u*color[0]+150u*color[1]+29u*color[2])>>8);
    }
#ifdef ESP_PLATFORM
    // 大图也定期让出 CPU，避免空闲任务看门狗饥饿。/ Yield during large images so idle watchdog tasks can run.
    if (!(++io->blocks % 32)) vTaskDelay(1);
#endif
    return 1;
}
static bool jpeg_decode(const uint8_t* data, size_t len, size_t budget,
                        uint8_t** pixels, uint16_t* width, uint16_t* height) {
    void* work = heap_caps_malloc(8192, PSRAM_CAPS);
    if (!work) return false;
    JDEC jd;
    jpeg_io_t io = {.data=data, .len=len};
    bool ok = false;
    if (jd_prepare(&jd, jpeg_read, work, 8192, &io) != JDR_OK || !jd.width || !jd.height ||
        jd.width > 8192 || jd.height > 8192 || (uint64_t)jd.width * jd.height > 16u * 1024u * 1024u) goto done;
    io.width = jd.width; io.height = jd.height;
    if (io.width > 648) { io.height = io.height * 648 / io.width; io.width = 648; }
    if (io.height > 1000) { io.width = io.width * 1000 / io.height; io.height = 1000; }
    if (!io.width) io.width = 1;
    if (!io.height) io.height = 1;
    if ((size_t)io.width * io.height > budget) goto done;
    unsigned scale = 0;
    // 阅读插图允许至多两倍放大，优先在 JPEG 解码阶段降采样。/ Allow at most 2x enlargement to reduce work during JPEG decoding.
    while (scale < 3 && ((unsigned)jd.width >> (scale + 1)) >= (io.width + 1) / 2 && ((unsigned)jd.height >> (scale + 1)) >= (io.height + 1) / 2) ++scale;
    io.scaled_width = jd.width >> scale; io.scaled_height = jd.height >> scale;
    io.out = heap_caps_malloc((size_t)io.width * io.height, PSRAM_CAPS);
    if (!io.out) goto done;
    memset(io.out, 255, (size_t)io.width * io.height);
    ok = jd_decomp(&jd, jpeg_write, (uint8_t)scale) == JDR_OK;
    if (ok) { *pixels = io.out; *width = (uint16_t)io.width; *height = (uint16_t)io.height; }
    else free(io.out);
done:
    free(work);
    return ok;
}

bool book_image_decode(const uint8_t* data, size_t len, size_t budget,
                       uint8_t** pixels, uint16_t* width, uint16_t* height) {
    if (!pixels || !width || !height) return false;
    *pixels = NULL; *width = *height = 0;
    if (!data || !len || len > 2u * 1024u * 1024u || allocated) return false;
    if (png_decode(data,len,budget,pixels,width,height)) return true;
    // 基线 JPEG 优先使用 MCU 分块缩小；小型渐进 JPEG 仍可走有界完整解码。
    // Prefer MCU-scaled baseline JPEG; bounded full decoding still handles small progressive JPEGs.
    if (len >= 2 && data[0] == 0xff && data[1] == 0xd8 && jpeg_decode(data, len, budget, pixels, width, height)) return true;
    int w, h, channels;
    if (!stbi_info_from_memory(data, (int)len, &w, &h, &channels) ||
        w <= 0 || h <= 0 || (uint64_t)w * h > 1024u * 1024u) return false;
    int dw = w, dh = h;
    if (dw > 648) { dh = (int)((int64_t)dh * 648 / dw); dw = 648; }
    if (dh > 1000) { dw = (int)((int64_t)dw * 1000 / dh); dh = 1000; }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    size_t size = (size_t)dw * dh;
    if (size > budget) return false;
    uint8_t* gray = stbi_load_from_memory(data, (int)len, &w, &h, &channels, 2);
    if (!gray) return false;
    uint8_t* out = heap_caps_malloc(size, PSRAM_CAPS);
    if (out) {
        for (int y = 0; y < dh; ++y) for (int x = 0; x < dw; ++x) {
            size_t at = ((size_t)((int64_t)y * h / dh) * w + (size_t)((int64_t)x * w / dw)) * 2;
            unsigned alpha = gray[at + 1];
            out[(size_t)y * dw + x] = (uint8_t)((gray[at] * alpha + 255u * (255u - alpha) + 127u) / 255u);
        }
    }
    stbi_image_free(gray);
    if (!out) return false;
    *pixels = out; *width = (uint16_t)dw; *height = (uint16_t)dh;
    return true;
}
