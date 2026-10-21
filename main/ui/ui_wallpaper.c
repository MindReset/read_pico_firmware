/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：壁纸预览与锁屏共用的 TF 图片缩放和灰阶绘制。
 * English: Shared SD image scaling and grayscale rendering for wallpaper preview and lock.
 */

#include "ui_wallpaper.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "book_cover.h"
#include "esp_heap_caps.h"
#include "ui_image_dither.h"
#include "ui_kit.h"
#include "ui_wallpaper_crop.h"

static bool inside_rounded(EpdRect area, int radius, int x, int y) {
    if (radius <= 0) return true;
    int local_x = x - area.x, local_y = y - area.y;
    int center_x = local_x < radius ? radius - 1 :
                   local_x >= area.width - radius ? area.width - radius : local_x;
    int center_y = local_y < radius ? radius - 1 :
                   local_y >= area.height - radius ? area.height - radius : local_y;
    int dx = local_x - center_x, dy = local_y - center_y;
    return dx * dx + dy * dy <= radius * radius;
}

// 仅头像自动裁掉明显的白色留边，并保持主体周围少量呼吸空间。
// Trim obvious white avatar margins while keeping a small amount of space around the subject.
static bool avatar_content_crop(const uint8_t *gray, unsigned width, unsigned height,
                                EpdRect area, int *crop_x, int *crop_y, int *crop_w, int *crop_h) {
    if (width < 24 || height < 24) return false;
    unsigned min_x = width, min_y = height, max_x = 0, max_y = 0, ink = 0;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            if (gray[(size_t)y * width + x] >= 245) continue;
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
            ++ink;
        }
    }
    if (ink < (uint64_t)width * height / 50) return false;
    int pad = (int)(width < height ? width : height) / 32;
    if (pad < 2) pad = 2;
    int box_w = (int)(max_x - min_x + 1) + pad * 2;
    int box_h = (int)(max_y - min_y + 1) + pad * 2;
    int w = box_w, h = box_h;
    if ((int64_t)w * area.height < (int64_t)h * area.width)
        w = (int)(((int64_t)h * area.width + area.height - 1) / area.height);
    else
        h = (int)(((int64_t)w * area.height + area.width - 1) / area.width);
    if (w > (int)width || h > (int)height ||
        ((int64_t)w * 10 > (int64_t)width * 9 &&
         (int64_t)h * 10 > (int64_t)height * 9)) return false;
    int x = ((int)min_x + (int)max_x + 1 - w) / 2;
    int y = ((int)min_y + (int)max_y + 1 - h) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > (int)width) x = (int)width - w;
    if (y + h > (int)height) y = (int)height - h;
    *crop_x = x; *crop_y = y; *crop_w = w; *crop_h = h;
    return true;
}

static bool image_pixels(uint8_t *fb, uint8_t *pixels, const char *path, EpdRect area, int radius) {
    if ((!fb && !pixels) || !path || strncmp(path, "/sdcard/", 8) ||
        area.width <= 0 || area.height <= 0 ||
        area.x < 0 || area.y < 0 ||
        area.x + area.width > UI_LOCK_WIDTH || area.y + area.height > UI_LOCK_HEIGHT)
        return false;
    const char *ext = strrchr(path, '.');
    bool png = ext && !strcasecmp(ext, ".png");
    if (!ext || (!png && strcasecmp(ext, ".jpg") && strcasecmp(ext, ".jpeg"))) return false;
    struct stat st;
    if (stat(path, &st) || st.st_size <= 0 || (uint64_t)st.st_size > BOOK_IMAGE_FILE_MAX) return false;
    unsigned source_w = 0, source_h = 0;
    bool ok = book_image_file_dimensions(path, png, &source_w, &source_h);
    unsigned width = (unsigned)area.width, height = (unsigned)area.height;
    bool source_wider = ok && (uint64_t)source_w * height > (uint64_t)source_h * width;
    if (source_wider) width = (unsigned)(((uint64_t)height * source_w + source_h - 1) / source_h);
    else if (ok) height = (unsigned)(((uint64_t)width * source_h + source_w - 1) / source_w);
    if (!width) width = 1;
    if (!height) height = 1;
    // 保持原始宽高比，并受解码器边长限制；宽图只缩一次，避免过度降采样。
    // Preserve source aspect ratio within decoder limits; avoid repeatedly downsampling wide photos.
    if (width > 1216u) {
        width = 1216u;
        height = (unsigned)(((uint64_t)width * source_h + source_w / 2u) / source_w);
    } else if (height > 1216u) {
        height = 1216u;
        width = (unsigned)(((uint64_t)height * source_w + source_h / 2u) / source_h);
    }
    if (!width) width = 1;
    if (!height) height = 1;
    // 缓冲区最多一屏像素；最终从原比例灰阶图居中裁切到目标区域。
    // Cap the buffer to one screen and center-crop the aspect-preserved gray image to the target.
    while (ok && (width > 1216u || height > 1216u ||
                  (uint64_t)width * height > 684u * 1216u)) {
        width = (width * 3u + 3u) / 4u;
        height = (height * 3u + 3u) / 4u;
    }
    uint8_t *gray = ok ? heap_caps_malloc((size_t)width * height, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    if (!gray) ok = false;
    if (ok) ok = book_image_file_grayscale(path, png, width, height, gray);
    if (!ok) { free(gray); return false; }
    ui_wallpaper_crop_t center = ui_wallpaper_center_crop(width, height,
                                                           (unsigned)area.width, (unsigned)area.height);
    int crop_x = (int)center.x, crop_y = (int)center.y;
    int crop_w = (int)center.width, crop_h = (int)center.height;
    if (radius > 0) avatar_content_crop(gray, width, height, area,
                                        &crop_x, &crop_y, &crop_w, &crop_h);

    for (int y = area.y; y < area.y + area.height; ++y)
        for (int x = area.x; x < area.x + area.width; ++x) {
            if (!pixels && !inside_rounded(area, radius, x, y)) continue;
            int source_x = crop_x + (int)((int64_t)(x - area.x) * crop_w / area.width);
            int source_y = crop_y + (int)((int64_t)(y - area.y) * crop_h / area.height);
            uint8_t value = gray[(size_t)source_y * width + source_x];
            if (pixels) pixels[(size_t)(y-area.y)*area.width + x-area.x] = value;
            else epd_draw_pixel(x,y,ui_image_dither_gray(value,x,y),fb);
        }
    free(gray);
    return true;
}

bool ui_wallpaper_draw(uint8_t *fb, const char *path, EpdRect area) {
    return image_pixels(fb, NULL, path, area, 0);
}

bool ui_wallpaper_draw_rounded(uint8_t *fb, const char *path, EpdRect area, int radius) {
    if (radius < 0 || radius > area.width / 2 || radius > area.height / 2) return false;
    return image_pixels(fb, NULL, path, area, radius);
}

void ui_wallpaper_draw_avatar(uint8_t *fb, const uint8_t *pixels, EpdRect area, int radius) {
    if (!fb || !pixels || area.width != area.height || area.width < 1 || area.width > 128) return;
    for (int y=0;y<area.height;++y) for (int x=0;x<area.width;++x) {
        if (inside_rounded(area,radius,area.x+x,area.y+y))
            epd_draw_pixel(area.x+x,area.y+y,ui_image_dither_gray(pixels[(size_t)y*area.width+x],area.x+x,area.y+y),fb);
    }
}

#ifndef UI_WALLPAPER_CACHE_PARENT
#define UI_WALLPAPER_CACHE_PARENT "/sdcard/.readpico"
#endif
#define WALL_LOCK_CACHE UI_WALLPAPER_CACHE_PARENT "/locks/wallpaper.bin"
#define WALL_LOCK_BYTES (684u*1216u/2u)
#define WALL_LOCK_MAGIC UINT32_C(0x574c4b31)
#define WALL_LOCK_VERSION 1u
// 只缓存最终16级抖动画布到卡；借用锁屏画布，不常驻第二份整帧。
// Persist final 16-level dithered art on SD, borrowing the lock canvas instead of retaining another frame.
typedef struct {
    uint32_t magic,version,width,height,rotation,bytes;
    uint64_t key,checksum;
} wallpaper_cache_t;
static wallpaper_cache_t s_wall_pending;
static const uint8_t *s_wall_pending_fb;
static uint64_t wallpaper_hash(uint64_t h,const void *data,size_t size) {
    const uint8_t *p=data;
    while (size--) h=(h^*p++)*UINT64_C(1099511628211);
    return h;
}
static uint64_t wallpaper_key(const char *path,const struct stat *st) {
    uint64_t h=wallpaper_hash(UINT64_C(14695981039346656037),path,strlen(path)+1);
    int64_t size=st->st_size,modified=st->st_mtime;
    h=wallpaper_hash(h,&size,sizeof(size));
    return wallpaper_hash(h,&modified,sizeof(modified));
}
bool ui_wallpaper_prepare_lock(uint8_t *fb,const char *path) {
    s_wall_pending_fb=NULL;
    struct stat st;
    if (!fb || !path || strncmp(path,"/sdcard/",8) || stat(path,&st) || !S_ISREG(st.st_mode) || st.st_size<=0) return false;
    wallpaper_cache_t wanted={.magic=WALL_LOCK_MAGIC,.version=WALL_LOCK_VERSION,
        .width=UI_LOCK_WIDTH,.height=UI_LOCK_HEIGHT,.rotation=epd_get_rotation(),
        .bytes=WALL_LOCK_BYTES,.key=wallpaper_key(path,&st)};
    FILE *file=fopen(WALL_LOCK_CACHE,"rb");
    if (file) {
        wallpaper_cache_t h;
        bool good=fread(&h,1,sizeof(h),file)==sizeof(h) && h.magic==wanted.magic && h.version==wanted.version &&
            h.key==wanted.key && h.width==wanted.width && h.height==wanted.height && h.rotation==wanted.rotation &&
            h.bytes==wanted.bytes && fread(fb,1,WALL_LOCK_BYTES,file)==WALL_LOCK_BYTES && fgetc(file)==EOF &&
            h.checksum==wallpaper_hash(UINT64_C(14695981039346656037),fb,WALL_LOCK_BYTES);
        fclose(file);
        if (good) return true;
    }
    if (!ui_wallpaper_draw(fb,path,(EpdRect){0,0,UI_LOCK_WIDTH,UI_LOCK_HEIGHT})) return false;
    struct stat after;
    if (!stat(path,&after) && after.st_size==st.st_size && after.st_mtime==st.st_mtime) {
        s_wall_pending=wanted; s_wall_pending_fb=fb;
    }
    return true;
}
void ui_wallpaper_cache_commit(const uint8_t *fb) {
    bool pending=fb && fb==s_wall_pending_fb;
    s_wall_pending_fb=NULL;
    if (!pending) return;
    if ((mkdir(UI_WALLPAPER_CACHE_PARENT,0777) && errno!=EEXIST) ||
        (mkdir(UI_WALLPAPER_CACHE_PARENT "/locks",0777) && errno!=EEXIST)) return;
    wallpaper_cache_t h=s_wall_pending;
    h.checksum=wallpaper_hash(UINT64_C(14695981039346656037),fb,WALL_LOCK_BYTES);
    FILE *file=fopen(WALL_LOCK_CACHE ".tmp","wb");
    if (!file) return;
    bool good=fwrite(&h,1,sizeof(h),file)==sizeof(h) && fwrite(fb,1,WALL_LOCK_BYTES,file)==WALL_LOCK_BYTES;
    if (fclose(file)) good=false;
    if (good && !rename(WALL_LOCK_CACHE ".tmp",WALL_LOCK_CACHE)) return;
    (void)remove(WALL_LOCK_CACHE ".tmp");
}

#define AVATAR_CACHE_MAGIC UINT32_C(0x41565431)
// 每个使用尺寸只存当前头像；灰阶像素与模式/对比度无关，不增加常驻内存。
// Store one current avatar per used size; raw grays are mode/contrast independent without added resident memory.
static bool avatar_cache_header(const char *path,int side,wallpaper_cache_t *header,char cache_path[160]) {
    struct stat st;
    const char *ext=path?strrchr(path,'.'):NULL;
    if(!path||strncmp(path,"/sdcard/",8)||side<1||side>128||!ext||
       (strcasecmp(ext,".png")&&strcasecmp(ext,".jpg")&&strcasecmp(ext,".jpeg"))||
       stat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||(uint64_t)st.st_size>BOOK_IMAGE_FILE_MAX)return false;
    *header=(wallpaper_cache_t){.magic=AVATAR_CACHE_MAGIC,.version=1,.width=side,.height=side,.bytes=(uint32_t)side*side,.key=wallpaper_key(path,&st)};
    snprintf(cache_path,160,UI_WALLPAPER_CACHE_PARENT "/avatars/avatar-%d.bin",side);
    return true;
}
bool ui_wallpaper_avatar_cached(const char *path,uint8_t *pixels,int side) {
    wallpaper_cache_t wanted,h;char cache_path[160];
    if(!pixels||!avatar_cache_header(path,side,&wanted,cache_path))return false;
    FILE *file=fopen(cache_path,"rb");if(!file)return false;
    bool good=fread(&h,1,sizeof(h),file)==sizeof(h)&&h.magic==wanted.magic&&h.version==wanted.version&&
        h.width==wanted.width&&h.height==wanted.height&&h.rotation==0&&h.bytes==wanted.bytes&&h.key==wanted.key&&
        fread(pixels,1,wanted.bytes,file)==wanted.bytes&&fgetc(file)==EOF&&!ferror(file)&&
        h.checksum==wallpaper_hash(UINT64_C(14695981039346656037),pixels,wanted.bytes);
    fclose(file);return good;
}
bool ui_wallpaper_prepare_avatar(const char *path,uint8_t *pixels,int side) {
    wallpaper_cache_t h,after;char cache_path[160],after_path[160];
    if(!pixels||!avatar_cache_header(path,side,&h,cache_path))return false;
    if(ui_wallpaper_avatar_cached(path,pixels,side))return true;
    if(!image_pixels(NULL,pixels,path,(EpdRect){0,0,side,side},1))return false;
    // 文件在解码期间变化或卡不可写时直接使用当前像素，不存过期/半写的文件。
    // Use decoded pixels without saving stale or partial files if the source changes or storage is unwritable.
    if(!avatar_cache_header(path,side,&after,after_path)||after.key!=h.key||
       (mkdir(UI_WALLPAPER_CACHE_PARENT,0777)&&errno!=EEXIST)||
       (mkdir(UI_WALLPAPER_CACHE_PARENT "/avatars",0777)&&errno!=EEXIST))return true;
    h.checksum=wallpaper_hash(UINT64_C(14695981039346656037),pixels,h.bytes);
    char temp[168];snprintf(temp,sizeof(temp),"%s.tmp",cache_path);
    FILE *file=fopen(temp,"wb");if(!file)return true;
    bool good=fwrite(&h,1,sizeof(h),file)==sizeof(h)&&fwrite(pixels,1,h.bytes,file)==h.bytes;
    if(fclose(file))good=false;
    if(!good||rename(temp,cache_path))(void)remove(temp);
    return true;
}
