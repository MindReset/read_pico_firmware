/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：D 版书架拼贴：原比例圆角封面共用15度旋转，原生系统字形叠白色描边。
 * English: D collage: aspect-preserved rounded covers share a 15-degree rotation, with native system glyphs and a white sticker edge.
 * 冻结：不接入在线书源、不切阅读字体、不全量载入字库；仅保留有界工作区和 TF 缓存。
 * Frozen: No online sources, reader-font changes or whole-font loads; bounded workspaces and SD cache only.
 * 用户修订：复用单次扫描的文件信息，只读封面头取得尺寸；锁屏显示后再提交新整帧缓存。
 * User revision: reuse scan metadata, read cover headers for dimensions and commit fresh frame caches after presentation.
 * 冻结：不改拼贴像素、缓存键或校验；延后写入仅引用调用者仍持有的完整画布，不增加整帧副本。
 * Frozen: Keep artwork, cache keys and checksums. Deferred persistence borrows a caller-owned complete frame, never another full copy.
 */
#include "book_lock_collage.h"
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "boot_state.h"
#include "book_cover.h"
#include "book_cover_auto.h"
#include "book_epub.h"
#include "book_progress.h"
#include "book_store.h"
#include "book_title.h"
#include "epdiy.h"
#include "esp_heap_caps.h"
#include "ttf_font.h"
#include "ui_image_dither.h"
#include "ui_kit.h"
#include "settings.h"
#include "nvs.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define LOCK_BOOKS 15
#define LOCK_DEPTH 12
#define LOCK_RESERVE (1024u * 1024u)
#define LOCK_IMAGE_PIXELS (128u * 1024u)
#define LOCK_FRAME_BYTES (684u * 1216u / 2u)
#ifndef BOOK_LOCK_CACHE_PARENT
#define BOOK_LOCK_CACHE_PARENT "/sdcard/.readpico"
#endif
#define LOCK_CACHE_DIR BOOK_LOCK_CACHE_PARENT "/locks"
#define LOCK_CACHE_VERSION 1u
#define LOCK_RENDER_REVISION 4u
#define LOCK_CACHE_MAGIC UINT32_C(0x4c434431)
#define LOCK_COVER_HEIGHT 334.f
#define LOCK_GAP 18.f
#define LOCK_RADIUS 14.f
#define LOCK_COS 0.9659258263f
#define LOCK_SIN 0.2588190451f

typedef struct {
    char path[BOOK_STORE_PATH_MAX], title[256];
    uint32_t size, recent;
    int64_t modified;
    uint64_t key;
    unsigned source_w, source_h, width, height;
} lock_book_t;
typedef struct { DIR *dir; char path[BOOK_STORE_PATH_MAX]; } lock_dir_t;
typedef struct {
    lock_book_t books[LOCK_BOOKS];
    book_store_root_t roots[BOOK_STORE_ROOT_MAX];
    unsigned count, used;
    int roots_count;
    nvs_handle_t hidden;
    bool cache_complete;
    const char *font_path;
    struct stat font_stat;
    bool font_stat_valid;
    int font_weight;
} lock_library_t;
typedef struct {
    uint32_t magic, version;
    uint64_t key, checksum;
    uint32_t width, height, source_w, source_h, rotation, bytes;
} lock_cache_t;
typedef struct { float x, y, w; unsigned index; } lock_placement_t;
static lock_library_t *s_library_index;
static unsigned s_library_revision,s_library_root_count;
void book_lock_collage_invalidate(void) { free(s_library_index);s_library_index=NULL; }
static const uint8_t *s_pending_frame;
static lock_cache_t s_pending_cache;

static void lock_yield(unsigned at) {
#ifdef ESP_PLATFORM
    if (!(at & 31u)) vTaskDelay(1);
#else
    (void)at;
#endif
}
static void *lock_alloc(size_t bytes) {
    size_t available = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return bytes <= available && available - bytes >= LOCK_RESERVE
        ? heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
}
static uint64_t hash_bytes(uint64_t h, const void *bytes, size_t length) {
    const uint8_t *p = bytes;
    while (length--) h = (h ^ *p++) * UINT64_C(1099511628211);
    return h;
}
static uint64_t hash_text(uint64_t h, const char *text) {
    return hash_bytes(h, text, strlen(text) + 1);
}
static uint64_t file_key(uint64_t h, const char *path, const struct stat *st) {
    h = hash_text(h, path);
    if (st) {
        int64_t size = st->st_size, modified = st->st_mtime;
        h = hash_bytes(h, &size, sizeof(size));
        h = hash_bytes(h, &modified, sizeof(modified));
    }
    return h;
}
static bool hidden_book(nvs_handle_t h, const char *path) {
    if (!h) return false;
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        hash = (hash ^ *p) * UINT32_C(16777619);
    char key[11], stored[BOOK_STORE_PATH_MAX];
    snprintf(key, sizeof(key), "h_%08lx", (unsigned long)hash);
    size_t size = sizeof(stored);
    return nvs_get_str(h, key, stored, &size) == ESP_OK && !strcmp(stored, path);
}
static int book_order(const lock_book_t *a, const lock_book_t *b) {
    if (a->recent != b->recent) return a->recent > b->recent ? -1 : 1;
    int title = strcmp(a->title, b->title);
    return title ? title : strcmp(a->path, b->path);
}
static void collect_book(lock_library_t *library, const char *path,
                          const struct stat *st, const book_progress_t *saved) {
    const char *ext = strrchr(path, '.');
    if (!ext || (strcasecmp(ext, ".epub") && strcasecmp(ext, ".txt")) ||
        !S_ISREG(st->st_mode) || st->st_size < 0 || (uint64_t)st->st_size > UINT32_MAX ||
        strlen(path) >= BOOK_STORE_PATH_MAX || hidden_book(library->hidden, path)) return;
    // 只保留常读/最近的十五条；总数继续完整统计，目录和字体不作书籍计数。
    // Keep only fifteen recent books while counting every eligible title; exclude folders and fonts.
    lock_book_t candidate = {0};
    snprintf(candidate.path, sizeof(candidate.path), "%s", path);
    if (!book_title_from_path(path, candidate.title, sizeof(candidate.title))) return;
    if (library->count < UINT_MAX) ++library->count;
    candidate.source_w = BOOK_COVER_W; candidate.source_h = BOOK_COVER_H;
    candidate.size = (uint32_t)st->st_size;
    candidate.modified = st->st_mtime;
    book_progress_t progress;
    if (saved) candidate.recent = saved->last_open_s;
    else if (book_progress_load(path, candidate.size, &progress)) candidate.recent = progress.last_open_s;
    candidate.key = file_key(UINT64_C(14695981039346656037), path, st);
    candidate.key = hash_text(candidate.key, candidate.title);
    candidate.key = file_key(candidate.key, library->font_path,
        library->font_stat_valid ? &library->font_stat : NULL);
    candidate.key = hash_bytes(candidate.key, &library->font_weight, sizeof(library->font_weight));
    unsigned at = 0;
    while (at < library->used && book_order(&library->books[at], &candidate) <= 0) ++at;
    if (at >= LOCK_BOOKS) return;
    if (library->used < LOCK_BOOKS) ++library->used;
    memmove(&library->books[at + 1], &library->books[at],
            (library->used - at - 1) * sizeof(candidate));
    library->books[at] = candidate;
}
static void scan_root(lock_library_t *library, const char *root) {
    lock_dir_t *frames = lock_alloc((LOCK_DEPTH + 1) * sizeof(*frames));
    if (!frames) { library->cache_complete = false; return; }
    snprintf(frames[0].path, sizeof(frames[0].path), "%s", root);
    frames[0].dir = opendir(root);
    if (!frames[0].dir) library->cache_complete = false;
    int depth = frames[0].dir ? 0 : -1;
    unsigned visited = 0;
    while (depth >= 0) {
        errno = 0;
        struct dirent *entry = readdir(frames[depth].dir);
        if (!entry) {
            if (errno) library->cache_complete = false;
            closedir(frames[depth].dir); frames[depth--].dir = NULL; continue;
        }
        if (entry->d_name[0] == '.') continue;
        char path[BOOK_STORE_PATH_MAX];
        int length = snprintf(path, sizeof(path), "%s/%s", frames[depth].path, entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(path)) { library->cache_complete = false; continue; }
        struct stat st;
        // FAT/内置存储没有目录链接；宿主测试拒绝链接，目录栈在所有平台都有深度上限。
        // FAT/internal storage has no directory links; hosts reject symlinks and every platform bounds directory depth.
#ifdef ESP_PLATFORM
        int status = stat(path, &st);
#else
        int status = lstat(path, &st);
#endif
        if (status) { library->cache_complete = false; continue; }
        if (S_ISDIR(st.st_mode)) {
            if (depth >= LOCK_DEPTH) { library->cache_complete = false; continue; }
            DIR *child = opendir(path);
            if (child) {
                ++depth; frames[depth].dir = child;
                memcpy(frames[depth].path, path, (size_t)length + 1);
            } else library->cache_complete = false;
        } else collect_book(library, path, &st, NULL);
        lock_yield(++visited);
    }
    free(frames);
}
static bool backfill_book(const char *path, const book_progress_t *progress, void *opaque) {
    lock_library_t *library = opaque;
    for (int i = 0; i < library->roots_count; ++i) {
        size_t n = strlen(library->roots[i].path);
        if (!strncmp(path, library->roots[i].path, n) && path[n] == '/') return true;
    }
    struct stat st;
    if (!stat(path, &st) && st.st_size >= 0 && (uint64_t)st.st_size == progress->file_size)
        collect_book(library, path, &st, progress);
    return true;
}
// 只保留最近十五本及根内总数；阅读进度变化从NVS合并，目录结构变化才重新遍历TF。
// Retain only the top15 and root count; merge progress from NVS and rescan TF only after structural changes.
static bool index_progress(const char *path,const book_progress_t *progress,void *opaque) {
    lock_library_t *library=opaque;struct stat st;
    if(stat(path,&st)||st.st_size<0||(uint64_t)st.st_size!=progress->file_size||!S_ISREG(st.st_mode)||hidden_book(library->hidden,path))return true;
    bool inside=false,duplicate=false;
    for(int i=0;i<library->roots_count;++i){size_t n=strlen(library->roots[i].path);if(!strncmp(path,library->roots[i].path,n)&&path[n]=='/')inside=true;}
    for(unsigned i=0;i<library->used;++i)if(!strcmp(path,library->books[i].path))duplicate=true;
    unsigned count=library->count;
    if(!duplicate)collect_book(library,path,&st,progress);
    library->count=inside?count:count<UINT_MAX?count+1:count;
    return true;
}
static bool index_restore(lock_library_t *library) {
    if(!s_library_index||s_library_revision!=book_store_revision()||!library->cache_complete||
       library->roots_count!=s_library_index->roots_count)return false;
    for(int i=0;i<library->roots_count;++i)
        if(strcmp(library->roots[i].path,s_library_index->roots[i].path))return false;
    for(unsigned i=0;i<s_library_index->used;++i){
        lock_book_t *old=&s_library_index->books[i];struct stat st;book_progress_t progress={0};
        if(stat(old->path,&st)||!S_ISREG(st.st_mode)||hidden_book(library->hidden,old->path))return false;
        bool saved=st.st_size>=0&&(uint64_t)st.st_size<=UINT32_MAX&&book_progress_load(old->path,(uint32_t)st.st_size,&progress);
        // 清进度可能让此前未进入前十五的书补位，必须重新扫描保证顺序。
        // Clearing progress can promote an uncached book; rescan to preserve exact ranking.
        if(old->recent&&(!saved||progress.last_open_s<old->recent))return false;
        collect_book(library,old->path,&st,saved?&progress:NULL);
    }
    library->count=s_library_root_count;
    return book_progress_list(index_progress,library)==ESP_OK;
}
static lock_library_t *library_load(void) {
    lock_library_t *library=lock_alloc(sizeof(*library));if(!library)return NULL;
    library->cache_complete=true;library->font_path=ttf_font_path();
    library->font_stat_valid=!stat(library->font_path,&library->font_stat);library->font_weight=ttf_get_weight();
    (void)nvs_open("rp_shelf",NVS_READONLY,&library->hidden);
    (void)book_store_roots(library->roots,&library->roots_count);
    if(book_store_roots_degraded())library->cache_complete=false;
    unsigned root_count=s_library_root_count;
    if(!index_restore(library)){
        library->count=library->used=0;
        for(int i=0;i<library->roots_count;++i)scan_root(library,library->roots[i].path);
        root_count=library->count;
        if(book_progress_list(backfill_book,library)!=ESP_OK)library->cache_complete=false;
    }
    if(library->hidden)nvs_close(library->hidden);
    library->hidden=0;
    if(library->cache_complete){
        if(!s_library_index)s_library_index=lock_alloc(sizeof(*s_library_index));
        if(s_library_index){*s_library_index=*library;s_library_revision=book_store_revision();s_library_root_count=root_count;}
    }else book_lock_collage_invalidate();
    return library;
}
static bool cache_directory(void) {
    if (mkdir(BOOK_LOCK_CACHE_PARENT, 0777) && errno != EEXIST) return false;
    return !mkdir(LOCK_CACHE_DIR, 0777) || errno == EEXIST;
}
static void cache_write(const char *path, lock_cache_t header, const uint8_t *data) {
    if (!cache_directory()) return;
    char temporary[128];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) return;
    header.checksum = hash_bytes(UINT64_C(14695981039346656037), data, header.bytes);
    FILE *file = fopen(temporary, "wb");
    if (!file) return;
    bool good = fwrite(&header, 1, sizeof(header), file) == sizeof(header) &&
                fwrite(data, 1, header.bytes, file) == header.bytes;
    if (fclose(file)) good = false;
    if (good && !rename(temporary, path)) return;
    (void)remove(temporary);
}
static bool cache_header(FILE *file, lock_cache_t *header, uint64_t key) {
    return fread(header, 1, sizeof(*header), file) == sizeof(*header) &&
        header->magic == LOCK_CACHE_MAGIC && header->version == LOCK_CACHE_VERSION && header->key == key;
}
static void image_cache_path(const lock_book_t *book, char path[112]) {
    // 同一本书只占一个缓存文件；新内容原子替换，避免按版本累积文件。
    // Each book owns one cache file; replace new content atomically without accumulating versions.
    uint64_t path_hash = hash_text(UINT64_C(14695981039346656037), book->path);
    snprintf(path, 112, LOCK_CACHE_DIR "/%016llx-cover.bin", (unsigned long long)path_hash);
}
static bool cover_header_valid(const lock_cache_t *h) {
    return h->width && h->height && h->width <= 1216 && h->height <= 1216 &&
        h->source_w && h->source_h && h->source_w <= 65535 && h->source_h <= 65535 &&
        (uint64_t)h->width * h->height == h->bytes && h->bytes <= LOCK_IMAGE_PIXELS;
}
static void cover_dimensions_set(lock_book_t *book, const lock_cache_t *h) {
    book->width = h->width; book->height = h->height;
    book->source_w = h->source_w; book->source_h = h->source_h;
}
static bool cover_dimensions_read(lock_book_t *book) {
    char path[112]; image_cache_path(book, path);
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    lock_cache_t header;
    bool ok = cache_header(file, &header, book->key) && cover_header_valid(&header) &&
        !fseek(file, 0, SEEK_END) && ftell(file) == (long)(sizeof(header) + header.bytes);
    fclose(file);
    // 尺寸不代表像素已校验；正式绘制仍完整读取并核对校验和。
    // Dimensions do not authenticate pixels; the drawing pass still reads and checks the full payload.
    if (ok) cover_dimensions_set(book, &header);
    return ok;
}
static uint8_t *cover_read(lock_book_t *book) {
    char path[112]; image_cache_path(book, path);
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    lock_cache_t header;
    uint8_t *gray = NULL;
    if (cache_header(file, &header, book->key) && cover_header_valid(&header)) {
        gray = lock_alloc(header.bytes);
        if (gray && (fread(gray, 1, header.bytes, file) != header.bytes || fgetc(file) != EOF ||
            hash_bytes(UINT64_C(14695981039346656037), gray, header.bytes) != header.checksum)) {
            free(gray); gray = NULL;
        }
        if (gray) {
            cover_dimensions_set(book, &header);
        }
    }
    fclose(file);
    return gray;
}
static uint8_t *cover_load_bounded(lock_book_t *book, bool *complete, size_t limit) {
    uint8_t *gray = cover_read(book);
    if (gray) return gray;
    uint8_t *data = NULL; size_t bytes = 0; bool png = false;
    unsigned sw = BOOK_COVER_W, sh = BOOK_COVER_H;
    bool decoded = false;
    size_t available = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t budget = available > LOCK_RESERVE + 512u * 1024u ? available - LOCK_RESERVE - 512u * 1024u : 0;
    if (budget > limit) budget = limit;
    const char *ext = strrchr(book->path, '.');
    esp_err_t error = ESP_ERR_NOT_FOUND;
    if (budget && ext && !strcasecmp(ext, ".epub") && pico_boot_asset_allowed(book->path))
        error = book_epub_cover_bounded(book->path, &data, &bytes, &png, budget);
    if (error == ESP_OK && book_image_dimensions(data, bytes, png, &sw, &sh) &&
        sw && sh && sw <= 65535 && sh <= 65535) {
        unsigned h = 334, w = (unsigned)(((uint64_t)h * sw + sh / 2u) / sh);
        if (!w) w = 1;
        while (w > 1216 || h > 1216 || (uint64_t)w * h > LOCK_IMAGE_PIXELS) {
            w = (w + 1) / 2; h = (h + 1) / 2;
        }
        gray = lock_alloc((size_t)w * h);
        decoded = gray && book_image_grayscale(data, bytes, png, w, h, gray);
        if (decoded) { book->width = w; book->height = h; }
        else { free(gray); gray = NULL; }
    }
    free(data);
    if (!decoded) {
        // 内存/格式错误不将替代封面固化；下一次可安全重试，不写启动恢复入口。
        // Never persist substitutes for OOM or format failures; later locks may safely retry without boot-resume changes.
        if (error != ESP_ERR_NOT_FOUND) *complete = false;
        sw = BOOK_COVER_W; sh = BOOK_COVER_H;
        book->width = sw; book->height = sh;
        gray = lock_alloc((size_t)sw * sh);
        if (gray && !book_auto_cover_render(book->path, book->title, "", sw, sh, gray)) {
            free(gray); gray = NULL;
        }
    }
    book->source_w = sw; book->source_h = sh;
    if (gray && (decoded || error == ESP_ERR_NOT_FOUND)) {
        char path[112]; image_cache_path(book, path);
        lock_cache_t header = {.magic = LOCK_CACHE_MAGIC, .version = LOCK_CACHE_VERSION,
            .key = book->key, .width = book->width, .height = book->height,
            .source_w = sw, .source_h = sh, .bytes = book->width * book->height};
        cache_write(path, header, gray);
    }
    if (!gray) *complete = false;
    return gray;
}
static uint8_t *cover_load(lock_book_t *book, bool *complete) {
    return cover_load_bounded(book, complete, 4u * 1024u * 1024u);
}
bool book_lock_collage_warm_cover(const char *path) {
    // 空闲时只处理一本，最多512KiB书内图片；大书或低内存留到原锁屏入口处理。
    // Idle work handles one book and at most 512KiB of embedded data; large books or low memory use the lock path.
    struct stat st;
    if (!path || strlen(path) >= BOOK_STORE_PATH_MAX || !ttf_font_ready() || !pico_boot_asset_allowed(path) ||
        stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || (uint64_t)st.st_size > 32u*1024u*1024u ||
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) < 3u*1024u*1024u) return false;
    lock_book_t book = {0};
    snprintf(book.path, sizeof(book.path), "%s", path);
    if (!book_title_from_path(path, book.title, sizeof(book.title))) return false;
    book.key = file_key(UINT64_C(14695981039346656037), path, &st);
    book.key = hash_text(book.key, book.title);
    const char *font_path = ttf_font_path(); struct stat font_stat;
    bool font_valid = !stat(font_path, &font_stat); int weight = ttf_get_weight();
    book.key = file_key(book.key, font_path, font_valid ? &font_stat : NULL);
    book.key = hash_bytes(book.key, &weight, sizeof(weight));
    bool complete = true;
    uint8_t *gray = cover_load_bounded(&book, &complete, 512u*1024u);
    bool ok = gray && complete;
    free(gray);
    return ok;
}

static unsigned make_placements(const lock_library_t *library, lock_placement_t out[30]) {
    if (!library->used) return 0;
    static const unsigned starts[6] = {10, 5, 0, 10, 5, 0};
    unsigned count = 0;
    for (unsigned row = 0; row < 6; ++row) {
        float width = LOCK_GAP * 4;
        for (unsigned col = 0; col < 5; ++col) {
            const lock_book_t *b = &library->books[(starts[row] + col) % library->used];
            width += LOCK_COVER_HEIGHT * b->source_w / b->source_h;
        }
        float x = -width / 2.f + (row == 1 ? 35.f : row == 2 ? -18.f : row == 3 ? 36.f : 0.f);
        for (unsigned col = 0; col < 5; ++col) {
            unsigned i = (starts[row] + col) % library->used;
            float w = LOCK_COVER_HEIGHT * library->books[i].source_w / library->books[i].source_h;
            out[count++] = (lock_placement_t){x, -794.f + row * (LOCK_COVER_HEIGHT + LOCK_GAP), w, i};
            x += w + LOCK_GAP;
        }
    }
    return count;
}
static bool inside_cover(float x, float y, float width) {
    if (x < 0 || y < 0 || x >= width || y >= LOCK_COVER_HEIGHT) return false;
    float radius = fminf(LOCK_RADIUS, width / 2.f);
    float cx = fmaxf(radius - x, x - (width - radius));
    float cy = fmaxf(radius - y, y - (LOCK_COVER_HEIGHT - radius));
    return cx <= 0 || cy <= 0 || cx * cx + cy * cy <= radius * radius;
}
static uint8_t sample_cover(const uint8_t *gray, const lock_book_t *book, float sx, float sy) {
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx > book->width - 1.f) sx = book->width - 1.f;
    if (sy > book->height - 1.f) sy = book->height - 1.f;
    unsigned x = (unsigned)sx, y = (unsigned)sy;
    unsigned nx = x + 1 < book->width ? x + 1 : x, ny = y + 1 < book->height ? y + 1 : y;
    float fx = sx - x, fy = sy - y;
    float a = gray[y * book->width + x] * (1.f - fx) + gray[y * book->width + nx] * fx;
    float b = gray[ny * book->width + x] * (1.f - fx) + gray[ny * book->width + nx] * fx;
    return (uint8_t)(a * (1.f - fy) + b * fy + .5f);
}
// 只给边缘整体偏浅的封面加细灰描边；沿旋转后的真实圆角，不画矩形底板。
// Outline covers with pale overall edges; follow the rotated rounded shape without a rectangular backing.
static bool pale_cover(const uint8_t *gray, const lock_book_t *book) {
    unsigned sum = 0;
    for (unsigned i = 0; i < 32; ++i) {
        unsigned x = i * (book->width - 1) / 31, y = i * (book->height - 1) / 31;
        sum += gray[x] + gray[(book->height - 1) * book->width + x] +
               gray[y * book->width] + gray[y * book->width + book->width - 1];
    }
    return sum >= 128u * 200u;
}
static bool cover_edge(float x, float y, float width) {
    const float inset = 1.3f;
    if (x < inset || y < inset || x >= width - inset || y >= LOCK_COVER_HEIGHT - inset) return true;
    float radius = fminf(LOCK_RADIUS, width / 2.f);
    float cx = fmaxf(radius - x, x - (width - radius));
    float cy = fmaxf(radius - y, y - (LOCK_COVER_HEIGHT - radius));
    return cx > 0 && cy > 0 && cx * cx + cy * cy >= (radius - inset) * (radius - inset);
}
// 固定的小投影沿真实圆角旋转；不用第二张全屏或逐像素工作区。
// A small shadow follows the rotated rounded cover, without a second frame or pixel workspace.
static void cover_shadow_draw(uint8_t *fb, lock_placement_t p) {
    const float offset_x = 3.f, offset_y = 6.f, spread = 11.f;
    float x0 = 342.f + LOCK_COS * p.x - LOCK_SIN * p.y + offset_x;
    float y0 = 608.f + LOCK_SIN * p.x + LOCK_COS * p.y + offset_y;
    int left = (int)fminf(684.f, fmaxf(0.f, floorf(x0 - LOCK_SIN * LOCK_COVER_HEIGHT - spread)));
    int right = (int)fminf(684.f, fmaxf(0.f, ceilf(x0 + LOCK_COS * p.w + spread)));
    int top = (int)fminf(1216.f, fmaxf(0.f, floorf(y0 - spread)));
    int bottom = (int)fminf(1216.f, fmaxf(0.f, ceilf(y0 + LOCK_SIN * p.w + LOCK_COS * LOCK_COVER_HEIGHT + spread)));
    float radius = fminf(LOCK_RADIUS, p.w / 2.f);
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            float dx = x + .5f - x0, dy = y + .5f - y0;
            float u = LOCK_COS * dx + LOCK_SIN * dy, v = -LOCK_SIN * dx + LOCK_COS * dy;
            float qx = fabsf(u - p.w / 2.f) - (p.w / 2.f - radius);
            float qy = fabsf(v - LOCK_COVER_HEIGHT / 2.f) - (LOCK_COVER_HEIGHT / 2.f - radius);
            float ax = fmaxf(qx, 0.f), ay = fmaxf(qy, 0.f);
            float distance = sqrtf(ax * ax + ay * ay) + fminf(fmaxf(qx, qy), 0.f) - radius;
            if (distance >= spread) continue;
            float fade = distance <= 0.f ? 1.f : (spread - distance) / spread;
            // 二次衰减让边缘轻柔，16物理灰阶抖动保留细小亮度差。
            // Quadratic falloff softens the edge; 16-level dithering retains fine luminance differences.
            uint8_t tone = (uint8_t)(255.f - 46.f * fade * fade);
            epd_draw_pixel(x, y, ui_image_dither_gray(tone, x, y), fb);
        }
        lock_yield((unsigned)y);
    }
}
static void cover_draw(uint8_t *fb, const uint8_t *gray, const lock_book_t *book, lock_placement_t p) {
    bool outline = pale_cover(gray, book);
    float x0 = 342.f + LOCK_COS * p.x - LOCK_SIN * p.y;
    float y0 = 608.f + LOCK_SIN * p.x + LOCK_COS * p.y;
    // 转为整数前先裁到屏幕，极宽封面和损坏的缓存不能使坐标溢出。
    // Clip before integer conversion so extreme aspect ratios cannot overflow coordinates.
    int left = (int)fminf(684.f, fmaxf(0.f, floorf(x0 - LOCK_SIN * LOCK_COVER_HEIGHT)));
    int right = (int)fminf(684.f, fmaxf(0.f, ceilf(x0 + LOCK_COS * p.w)));
    int top = (int)fminf(1216.f, fmaxf(0.f, floorf(y0)));
    int bottom = (int)fminf(1216.f, fmaxf(0.f, ceilf(y0 + LOCK_SIN * p.w + LOCK_COS * LOCK_COVER_HEIGHT)));
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            float dx = x + .5f - x0, dy = y + .5f - y0;
            float u = LOCK_COS * dx + LOCK_SIN * dy, v = -LOCK_SIN * dx + LOCK_COS * dy;
            if (!inside_cover(u, v, p.w)) continue;
            uint8_t tone = sample_cover(gray, book, u * book->width / p.w - .5f,
                                        v * book->height / LOCK_COVER_HEIGHT - .5f);
            // 源缓存为未抖动灰阶，最终屏幕坐标只量化一次；不跟随主页黑白快刷。
            // Source cache is undithered gray; quantize once at screen coordinates, independent of main-page BW modes.
            if (outline && cover_edge(u, v, p.w)) tone = 112;
            epd_draw_pixel(x, y, ui_image_dither_gray(tone, x, y), fb);
        }
        lock_yield((unsigned)y);
    }
}
static void truncate_name(char *text) {
    size_t length = strlen(text);
    if (!length) return;
    --length;
    while (length && ((unsigned char)text[length] & 0xc0u) == 0x80u) --length;
    text[length] = 0;
}
static bool sticker_draw(uint8_t *fb, const char *text, int px, const char *unit,
                          int unit_px, int x, int y, bool right_bottom) {
    const int edge = 10, padding = 12;
    int above = 0, below = 0, ua = 0, ub = 0;
    ttf_measure_line_px(px, text, &above, &below);
    int w = ttf_text_width_px(px, text), uw = 0;
    if (unit) { ttf_measure_line_px(unit_px, unit, &ua, &ub); uw = ttf_text_width_px(unit_px, unit) + 10; }
    int a = above > ua ? above : ua, b = below > ub ? below : ub;
    unsigned width = (unsigned)(w + uw + padding * 2), height = (unsigned)(a + b + padding * 2);
    if (!w || width > 684u || height > 180u) return false;
    uint8_t *mask = lock_alloc((size_t)width * height), *dilated = lock_alloc((size_t)width * height);
    if (!mask || !dilated) { free(mask); free(dilated); return false; }
    bool ok = ttf_text_mask_px(mask, width, height, padding, padding + a, px, text);
    if (unit) ok = ok && ttf_text_mask_px(mask, width, height, padding + w + 10, padding + a, unit_px, unit);
    if (!ok) { free(mask); free(dilated); return false; }
    int min_x = (int)width, min_y = (int)height, max_x = -1, max_y = -1;
    for (unsigned yy = 0; yy < height; ++yy) for (unsigned xx = 0; xx < width; ++xx)
        if (mask[yy * width + xx]) {
            if ((int)xx < min_x) min_x = xx;
            if ((int)xx > max_x) max_x = xx;
            if ((int)yy < min_y) min_y = yy;
            if ((int)yy > max_y) max_y = yy;
        }
    if (max_x < 0) { free(mask); free(dilated); return false; }
    int origin_x = right_bottom ? x - max_x - 1 : x - min_x;
    int origin_y = right_bottom ? y - max_y - 1 : y - min_y;
    // 两个线性滑窗生成连接的白色描边；字形按实际基线排，数字与小“本”共用基线。
    // Two linear sliding windows form a connected white edge; the number and small unit share one measured baseline.
    for (unsigned yy = 0; yy < height; ++yy) {
        unsigned hits = 0;
        for (int xx = -edge; xx < (int)width; ++xx) {
            if (xx + edge < (int)width && mask[yy * width + xx + edge]) ++hits;
            if (xx - edge - 1 >= 0 && mask[yy * width + xx - edge - 1]) --hits;
            if (xx >= 0) dilated[yy * width + xx] = hits != 0;
        }
    }
    for (unsigned xx = 0; xx < width; ++xx) {
        unsigned hits = 0;
        for (int yy = -edge; yy < (int)height; ++yy) {
            if (yy + edge < (int)height && dilated[(yy + edge) * width + xx]) ++hits;
            if (yy - edge - 1 >= 0 && dilated[(yy - edge - 1) * width + xx]) --hits;
            if (yy >= 0 && hits) epd_draw_pixel(origin_x + xx, origin_y + yy, 255, fb);
        }
    }
    for (unsigned yy = 0; yy < height; ++yy) for (unsigned xx = 0; xx < width; ++xx)
        if (mask[yy * width + xx]) epd_draw_pixel(origin_x + xx, origin_y + yy, ui_image_dither_gray(255 - mask[yy * width + xx], origin_x + xx, origin_y + yy), fb);
    free(mask); free(dilated);
    return true;
}
static bool captions_draw(uint8_t *fb, unsigned count) {
    char name[64], title[96], number[16];
    const int title_px = ttf_em_height_px(48), unit_px = ttf_em_height_px(27);
    snprintf(name, sizeof(name), "%s", app_settings_device_name());
    do {
        snprintf(title, sizeof(title), "%s 的书架", name);
        if (ttf_text_width_px(title_px, title) <= 600) break;
        truncate_name(name);
    } while (name[0]);
    snprintf(number, sizeof(number), "%u", count);
    int px = ttf_em_height_px(91);
    while (px > title_px && ttf_text_width_px(px, number) + ttf_text_width_px(unit_px, "本") + 44 > 600) --px;
    return sticker_draw(fb, title, title_px, NULL, 0, 42, 70, false) &&
           sticker_draw(fb, number, px, "本", unit_px, 642, 1170, true);
}
static bool collage_draw(uint8_t *fb, bool defer_cache) {
    s_pending_frame = NULL;
    if (!fb || !ttf_font_ready()) return false;
    lock_library_t *library = library_load();
    if (!library) return false;
    uint64_t key = hash_text(UINT64_C(14695981039346656037), app_settings_device_name());
    key = file_key(key, library->font_path, library->font_stat_valid ? &library->font_stat : NULL);
    int weight = library->font_weight, rotation = epd_get_rotation();
    key = hash_bytes(key, &weight, sizeof(weight)); key = hash_bytes(key, &rotation, sizeof(rotation));
    const uint32_t render_revision = LOCK_RENDER_REVISION;
    key = hash_bytes(key, &render_revision, sizeof(render_revision));
    key = hash_bytes(key, &library->count, sizeof(library->count));
    // 已排序的封面身份序列决定画面；时间戳本身不参与绘制，进度保存但顺序不变时复用整帧。
    // Ordered cover identities determine the artwork. Saving progress without changing that order
    // must not invalidate the full frame merely because a recent timestamp advanced.
    key = hash_bytes(key, &library->used, sizeof(library->used));
    for (unsigned i = 0; i < library->used; ++i) {
        key = hash_bytes(key, &library->books[i].key, sizeof(library->books[i].key));
    }
    FILE *cached = fopen(LOCK_CACHE_DIR "/collage.bin", "rb");
    if (cached) {
        lock_cache_t header;
        bool ok = library->cache_complete && cache_header(cached, &header, key) &&
            header.bytes == LOCK_FRAME_BYTES && header.width == 684 && header.height == 1216 &&
            header.rotation == (unsigned)rotation && fread(fb, 1, LOCK_FRAME_BYTES, cached) == LOCK_FRAME_BYTES &&
            fgetc(cached) == EOF && hash_bytes(UINT64_C(14695981039346656037), fb, LOCK_FRAME_BYTES) == header.checksum;
        fclose(cached);
        if (ok) { free(library); return true; }
    }
    memset(fb, 255, LOCK_FRAME_BYTES);
    // 缓存有效时仅读头取得原比例；不存在或头损坏时才解码，一次只保留一张图。
    // Read valid headers for original ratios; decode absent/bad caches one image at a time.
    for (unsigned i = 0; i < library->used; ++i) {
        if (cover_dimensions_read(&library->books[i])) continue;
        uint8_t *gray = cover_load(&library->books[i], &library->cache_complete);
        if (!gray) { free(library); return false; }
        free(gray);
    }
    lock_placement_t placements[30];
    unsigned n = make_placements(library, placements);
    for (unsigned p = 0; p < n; ++p) cover_shadow_draw(fb, placements[p]);
    for (unsigned i = 0; i < library->used; ++i) {
        const unsigned sw = library->books[i].source_w, sh = library->books[i].source_h;
        uint8_t *gray = cover_load(&library->books[i], &library->cache_complete);
        // 第二次读取失败或比例变化时交回安全回退，不能拉伸封面或固化半张锁屏。
        // A failed reread or changed ratio requests safe fallback, never stretched art or a partial persistent frame.
        if (!gray || library->books[i].source_w != sw || library->books[i].source_h != sh) {
            free(gray); free(library); return false;
        }
        for (unsigned p = 0; p < n; ++p)
            if (placements[p].index == i) cover_draw(fb, gray, &library->books[i], placements[p]);
        free(gray);
    }
    bool ok = captions_draw(fb, library->count);
    if (ok && library->cache_complete) {
        lock_cache_t header = {.magic = LOCK_CACHE_MAGIC, .version = LOCK_CACHE_VERSION,
            .key = key, .width = 684, .height = 1216, .rotation = rotation, .bytes = LOCK_FRAME_BYTES};
        if (defer_cache) { s_pending_cache = header; s_pending_frame = fb; }
        else cache_write(LOCK_CACHE_DIR "/collage.bin", header, fb);
    }
    free(library);
    return ok;
}

bool book_lock_collage_draw(uint8_t *fb) { return collage_draw(fb, false); }
bool book_lock_collage_prepare(uint8_t *fb) { return collage_draw(fb, true); }
void book_lock_collage_cache_commit(const uint8_t *fb) {
    bool pending = fb && s_pending_frame == fb;
    s_pending_frame = NULL;
    if (pending) cache_write(LOCK_CACHE_DIR "/collage.bin", s_pending_cache, fb);
}
