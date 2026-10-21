/* SPDX-License-Identifier: Apache-2.0 */
#include "book_cover.h"
#include "book_cover_auto.h"
#include "book_epub.h"
#include "book_title.h"
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "jpeg_decoder.h"
#include "JPEGDEC.h"
#include "png.h"
#if defined(ESP_PLATFORM) && CONFIG_JD_USE_ROM
#include "rom/tjpgd.h"
typedef unsigned int stream_jpeg_result_t;
typedef unsigned int stream_jpeg_size_t;
#else
#include "tjpgd.h"
typedef int stream_jpeg_result_t;
typedef size_t stream_jpeg_size_t;
#endif
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
static void decode_yield(unsigned at) {
#ifdef ESP_PLATFORM
    if ((at & 63u) == 0) vTaskDelay(1);
#else
    (void)at;
#endif
}

static void *cover_alloc(size_t n) {
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static uint8_t luminance(uint8_t r, uint8_t g, uint8_t b) {
    return (uint8_t)(((unsigned)r * 77 + (unsigned)g * 150 + (unsigned)b * 29) >> 8);
}

// 目标下标映射回源窗口坐标：中心对齐，Q8 定点，整数部分进 *out_at、小数部分 0..255 进
// *out_frac。原来每个目标像素直接取一个源像素，缩放后细节整块丢掉；中心对齐保证裁出来的
// 窗口正落在目标上，不会差半个像素。
// Map a destination index back to source-window coordinates: centre-aligned, Q8 fixed point, the
// integer part into *out_at and the 0..255 fraction into *out_frac. Sampling a single source pixel
// per destination pixel dropped detail wholesale once scaled down; centre-aligning keeps the crop
// window landing on the destination instead of half a pixel off.
static void scale_axis(unsigned at, unsigned dst_n, unsigned src_n, int *out_at, int *out_frac) {
    if (dst_n < 2 || src_n < 2) { *out_at = 0; *out_frac = 0; return; }
    // 源坐标 = ((2·at + 1)·src_n − dst_n) / (2·dst_n)。/ Source = ((2*at + 1)*src_n - dst_n) / (2*dst_n).
    int64_t q = (((2 * (int64_t)at + 1) * src_n - (int64_t)dst_n) * 256) / (2 * (int64_t)dst_n);
    if (q < 0) q = 0;
    const int64_t last = (int64_t)src_n - 1;
    if ((q >> 8) > last) q = last << 8;
    *out_at = (int)(q >> 8);
    *out_frac = (int)(q & 0xFF);
}

typedef struct { uint16_t at; uint8_t frac; } scale_step_t;

// 一根轴的映射表，放在 PSRAM：同一根轴上每个像素的权重都一样，算一次就够。
// One axis's map, kept in PSRAM: the weights repeat along an axis, so they are computed once.
static scale_step_t *scale_map(unsigned dst_n, unsigned src_n) {
    scale_step_t *map = cover_alloc(sizeof(scale_step_t) * (dst_n ? dst_n : 1));
    if (!map) return NULL;
    for (unsigned i = 0; i < dst_n; ++i) {
        int at = 0, frac = 0;
        scale_axis(i, dst_n, src_n, &at, &frac);
        map[i].at = (uint16_t)at;
        map[i].frac = (uint8_t)frac;
    }
    return map;
}

// 取相邻的下一个源下标，到边界就停住，相当于把边缘像素拉出去用。
// The next source index, clamped at the edge so the border pixel is stretched outwards.
static inline int scale_next(int at, unsigned src_n) {
    return (unsigned)(at + 1) < src_n ? at + 1 : at;
}

// 四点双线性：a b 在上、c d 在下，fx 与 fy 是 0..255 的权重。
// Four-point bilinear: a b on top, c d below, fx and fy are 0..255 weights.
static inline uint8_t bilinear4(int a, int b, int c, int d, int fx, int fy) {
    const int top = a + (((b - a) * fx + 128) >> 8);
    const int bot = c + (((d - c) * fx + 128) >> 8);
    return (uint8_t)(top + (((bot - top) * fy + 128) >> 8));
}

// RGB565 一个像素的亮度，双线性要连着取几个点所以单列。
// Luminance of one RGB565 pixel; separate because bilinear reads several per output pixel.
static inline int rgb565_gray(const uint8_t *row, unsigned at) {
    const uint8_t *p = row + (size_t)at * 2;
    const uint16_t color = (uint16_t)p[0] | (uint16_t)p[1] << 8;
    const uint8_t r = (uint8_t)((color >> 11) & 31);
    const uint8_t g = (uint8_t)((color >> 5) & 63);
    const uint8_t b = (uint8_t)(color & 31);
    return luminance((uint8_t)((r << 3) | (r >> 2)), (uint8_t)((g << 2) | (g >> 4)),
                     (uint8_t)((b << 3) | (b >> 2)));
}

book_crop_t book_cover_crop(unsigned src_width, unsigned src_height,
                            unsigned dst_width, unsigned dst_height) {
    book_crop_t crop = {0, 0, src_width, src_height};
    if (!src_width || !src_height || !dst_width || !dst_height) return crop;
    // 比较 dst_w/src_w 与 dst_h/src_h，用交叉相乘避免浮点。
    // Compare dst_w/src_w against dst_h/src_h by cross-multiplying, avoiding floating point.
    if ((uint64_t)dst_width * src_height >= (uint64_t)dst_height * src_width) {
        // 宽边受限：占满整宽，上下居中裁。/ Width-bound: keep the full width, crop top and bottom.
        unsigned height = (unsigned)((uint64_t)dst_height * src_width / dst_width);
        if (height < 1) height = 1;
        if (height > src_height) height = src_height;
        crop.height = height;
        crop.y = (src_height - height) / 2;
    } else {
        // 高边受限：占满整高，左右居中裁。/ Height-bound: keep the full height, crop left and right.
        unsigned width = (unsigned)((uint64_t)dst_width * src_height / dst_height);
        if (width < 1) width = 1;
        if (width > src_width) width = src_width;
        crop.width = width;
        crop.x = (src_width - width) / 2;
    }
    return crop;
}

/* ---- JPEG 帧头 / JPEG frame header ---- */

// 读 SOF 标记与帧尺寸，只走标记段、不解码。
// 0xC0 是基线，0xC2 是渐进式；ROM TJpgDec 只解基线，所以渐进式要交给 JPEGDEC。
// Walk the marker segments and read the SOF marker plus the frame size. 0xC0 is baseline and
// 0xC2 is progressive; the ROM TJpgDec decodes baseline only, so progressive frames go to
// JPEGDEC.


/* ---- 渐进式 JPEG / Progressive JPEG ---- */

// EIGHT_BIT_GRAYSCALE 下 pPixels 每像素一个字节。JPEGDEC 按 MCU 块回调：块内 stride 是
// iWidth，只有 iWidthUsed 列有效，坐标落在 JPEGDEC 缩放后的空间里。
// Under EIGHT_BIT_GRAYSCALE pPixels holds one byte per pixel. JPEGDEC calls back per MCU
// block: the stride inside a block is iWidth, only iWidthUsed columns are valid, and the
// coordinates live in JPEGDEC's reduced space.
typedef struct {
    uint8_t *plane;
    unsigned stride, height;
} jpegdec_plane_t;

static int jpegdec_collect(JPEGDRAW *draw) {
    jpegdec_plane_t *plane = draw->pUser;
    if (!plane || !plane->plane || draw->iBpp != 8 || !draw->pPixels || draw->iWidth < 1) return 0;
    const uint8_t *src = (const uint8_t *)draw->pPixels;
    const int columns = draw->iWidthUsed < draw->iWidth ? draw->iWidthUsed : draw->iWidth;
    for (int row = 0; row < draw->iHeight; ++row) {
        size_t y = (size_t)draw->y + row;
        if (draw->y + row < 0 || y >= plane->height) break;
        for (int column = 0; column < columns; ++column) {
            if (draw->x + column < 0) continue;
            size_t x = (size_t)draw->x + column;
            if (x >= plane->stride) continue;
            plane->plane[y * plane->stride + x] = src[(size_t)row * draw->iWidth + column];
        }
    }
    return 1;
}

// ESP-IDF 下 JPEGDEC 只提供 C++ 类，组件里有一层 extern "C" 包装。
// Under ESP-IDF JPEGDEC only offers the C++ class; the component wraps it with C linkage.
extern int jpegdec_gray_progressive(const uint8_t *data, int size, JPEG_DRAW_CALLBACK *draw,
                                    void *user, int *drawn_width, int *drawn_height);

// 渐进式只取 DC 扫描，JPEGDEC 因此固定输出 1/8 分辨率；平面尺寸由帧头算出，
// 与小平面一起缩到目标尺寸。对封面（176×240）来说已经足够，也避开了整幅系数数组。
// Progressive decoding reads the DC scan only, so JPEGDEC always emits 1/8 resolution. The plane
// size comes from the frame header and is resampled to the requested size: plenty for a 176x240
// cover, and it never holds the full coefficient array.
static bool jpegdec_gray(const uint8_t *data, size_t size, unsigned frame_width,
                         unsigned frame_height, unsigned out_width, unsigned out_height,
                         uint8_t *out) {
    if (!data || size < 4 || size > INT32_MAX || !frame_width || !frame_height) return false;
    jpegdec_plane_t plane = {
        .stride = (frame_width + 7) / 8,
        .height = (frame_height + 7) / 8,
    };
    if (plane.stride > 4096 || plane.height > 4096) return false;
    plane.plane = cover_alloc((size_t)plane.stride * plane.height);
    if (!plane.plane) return false;
    memset(plane.plane, 0xFF, (size_t)plane.stride * plane.height);
    const bool decoded = jpegdec_gray_progressive(data, (int)size, jpegdec_collect, &plane,
                                                 NULL, NULL) != 0;
    bool ok = false;
    if (decoded) {
        // 1/8 小平面同样按长边铺满 + 居中裁剪映射到目标。
        // The 1/8 plane maps to the target the same way: fill the longer side, centre-crop.
        const book_crop_t crop = book_cover_crop(plane.stride, plane.height, out_width, out_height);
        scale_step_t *xmap = scale_map(out_width, crop.width);
        scale_step_t *ymap = scale_map(out_height, crop.height);
        if (xmap && ymap) {
            for (unsigned y = 0; y < out_height; ++y) {
                const int sy = ymap[y].at, fy = ymap[y].frac;
                const int sy1 = scale_next(sy, crop.height);
                const uint8_t *r0 = plane.plane + (size_t)(crop.y + (unsigned)sy) * plane.stride;
                const uint8_t *r1 = plane.plane + (size_t)(crop.y + (unsigned)sy1) * plane.stride;
                for (unsigned x = 0; x < out_width; ++x) {
                    const int sx = xmap[x].at, fx = xmap[x].frac;
                    const int sx1 = scale_next(sx, crop.width);
                    out[y * out_width + x] = bilinear4(
                        r0[crop.x + (unsigned)sx], r0[crop.x + (unsigned)sx1],
                        r1[crop.x + (unsigned)sx], r1[crop.x + (unsigned)sx1], fx, fy);
                }
                decode_yield(y);
            }
            ok = true;
        }
        free(xmap);
        free(ymap);
    }
    free(plane.plane);
    return ok;
}

static bool jpeg_gray(const uint8_t *data, size_t size, unsigned out_width,
                      unsigned out_height, uint8_t *out) {
    if (size > UINT32_MAX) return false;
    uint8_t sof = 0;
    unsigned frame_width = 0, frame_height = 0;
    if (book_jpeg_frame(data, size, &sof, &frame_width, &frame_height) && sof == 0xC2)
        return jpegdec_gray(data, size, frame_width, frame_height, out_width, out_height, out);
    esp_jpeg_image_cfg_t cfg = { .indata = (uint8_t *)data, .indata_size = (uint32_t)size,
                                 .out_format = JPEG_IMAGE_FORMAT_RGB565 };
    esp_jpeg_image_output_t info = {0};
    size_t limit = out_width <= BOOK_COVER_W && out_height <= BOOK_COVER_H
        ? 3u * 1024u * 1024u : 6u * 1024u * 1024u;
    bool decoded = false;
    for (int scale = JPEG_IMAGE_SCALE_0; scale <= JPEG_IMAGE_SCALE_1_8; ++scale) {
        cfg.out_scale = scale;
        if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK) return false;
        if (!info.width || !info.height || !info.output_len || info.output_len > limit) continue;
        uint8_t *pixels = cover_alloc(info.output_len);
        if (!pixels) continue;
        cfg.outbuf = pixels;
        cfg.outbuf_size = info.output_len;
        if (esp_jpeg_decode(&cfg, &info) == ESP_OK) {
            // 长边铺满再居中裁剪，长宽比不同也不拉伸。
            // Fill by the longer side and centre-crop, so a different aspect ratio is not stretched.
            const book_crop_t crop = book_cover_crop(info.width, info.height, out_width, out_height);
            scale_step_t *xmap = scale_map(out_width, crop.width);
            scale_step_t *ymap = scale_map(out_height, crop.height);
            if (xmap && ymap) {
                for (unsigned y = 0; y < out_height; ++y) {
                    const int sy = ymap[y].at, fy = ymap[y].frac;
                    const int sy1 = scale_next(sy, crop.height);
                    const uint8_t *r0 = pixels + (size_t)(crop.y + (unsigned)sy) * info.width * 2;
                    const uint8_t *r1 = pixels + (size_t)(crop.y + (unsigned)sy1) * info.width * 2;
                    for (unsigned x = 0; x < out_width; ++x) {
                        const int sx = xmap[x].at, fx = xmap[x].frac;
                        const int sx1 = scale_next(sx, crop.width);
                        out[y * out_width + x] = bilinear4(
                            rgb565_gray(r0, crop.x + (unsigned)sx), rgb565_gray(r0, crop.x + (unsigned)sx1),
                            rgb565_gray(r1, crop.x + (unsigned)sx), rgb565_gray(r1, crop.x + (unsigned)sx1),
                            fx, fy);
                    }
                    decode_yield(y);
                }
                decoded = true;
            }
            free(xmap);
            free(ymap);
        }
        free(pixels);
        if (decoded) break;
    }
    return decoded;
}

typedef struct { const uint8_t *data; size_t size, pos; FILE *file; } png_input_t;
static void png_read_mem(png_structp png, png_bytep dst, png_size_t count) {
    png_input_t *input = png_get_io_ptr(png);
    if (count > input->size - input->pos) png_error(png, "truncated PNG");
    if (input->file) { if (fread(dst, 1, count, input->file) != count) png_error(png, "truncated PNG file"); }
    else memcpy(dst, input->data + input->pos, count);
    input->pos += count;
}
static bool png_gray_input(const uint8_t *data, size_t size, FILE *file, unsigned out_width,
                     unsigned out_height, uint8_t *out) {
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) return false;
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); return false; }
    uint8_t *volatile row = NULL;
    // 双线性要相邻两行，所以窗口内留两行灰度：行号按奇偶分别落在两个缓冲里。
    // Bilinear needs two adjacent rows, so two window rows of gray are kept, indexed by row parity.
    //
    // 这三个必须使用 volatile 并声明在 setjmp 之前：goto done 会跳过 setjmp 之后的初始化，在那里 free()
    // 未初始化的指针会崩。
    // These must be volatile and declared before setjmp: goto done skips the initialisers that follow it, and
    // freeing an uninitialised pointer there would crash.
    scale_step_t *volatile xmap = NULL, *volatile ymap = NULL;
    uint8_t *volatile rows[2] = {NULL, NULL};
    png_input_t input = {data, size, 0, file};
    bool ok = false;
    if (setjmp(png_jmpbuf(png))) goto done;
    png_set_read_fn(png, &input, png_read_mem);
    png_read_info(png, info);
    png_uint_32 width = png_get_image_width(png, info), height = png_get_image_height(png, info);
    if (!width || !height || width > 8192 || height > 8192) goto done;
    int color = png_get_color_type(png, info), depth = png_get_bit_depth(png, info);
    if (depth == 16) png_set_strip_16(png);
    if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if (!(color & PNG_COLOR_MASK_ALPHA) && !png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_filler(png, 0xff, PNG_FILLER_AFTER);
    png_read_update_info(png, info);
    png_size_t stride = png_get_rowbytes(png, info);
    if (stride < width * 4 || stride > 65536) goto done;
    row = cover_alloc(stride);
    if (!row) goto done;
    unsigned next_y = 0;
    // 流式读取时裁剪窗口同时作用于行与列：窗口外的行直接丢弃，列按窗口取样。
    // The streaming reader applies the crop window to rows and columns alike: rows outside the
    // window are dropped and columns are sampled inside it.
    const book_crop_t crop = book_cover_crop(width, height, out_width, out_height);
    if (crop.width && crop.height) {
        xmap = scale_map(out_width, crop.width);
        ymap = scale_map(out_height, crop.height);
        rows[0] = cover_alloc(crop.width);
        rows[1] = cover_alloc(crop.width);
    }
    if (xmap && ymap && rows[0] && rows[1]) {
        for (png_uint_32 y = 0; y < height; ++y) {
            png_read_row(png, row, NULL);
            decode_yield((unsigned)y);
            const int wy = (int)y - (int)crop.y;
            if (wy < 0 || wy >= (int)crop.height) continue;
            uint8_t *cur = rows[wy & 1];
            for (unsigned x = 0; x < crop.width; ++x) {
                const uint8_t *p = row + (size_t)(crop.x + x) * 4;
                const uint8_t gray = luminance(p[0], p[1], p[2]);
                cur[x] = (uint8_t)(((unsigned)gray * p[3] + 255u * (255u - p[3])) / 255u);
            }
            // 两行都齐了的目标行可以一次输出完：同一源行可能对应多个目标行（放大时）。
            // Every destination row whose two source rows are now present can be emitted: one source
            // row may serve several destination rows when scaling up.
            while (next_y < out_height) {
                const int sy = ymap[next_y].at, fy = ymap[next_y].frac;
                const int sy1 = scale_next(sy, crop.height);
                if (wy < sy1) break;
                const uint8_t *r0 = rows[sy & 1], *r1 = rows[sy1 & 1];
                for (unsigned x = 0; x < out_width; ++x) {
                    const int sx = xmap[x].at, fx = xmap[x].frac;
                    const int sx1 = scale_next(sx, crop.width);
                    out[next_y * out_width + x] = bilinear4(r0[sx], r0[sx1], r1[sx], r1[sx1], fx, fy);
                }
                ++next_y;
            }
        }
    }
    ok = next_y == out_height;
done:
    free(xmap);
    free(ymap);
    free(rows[0]);
    free(rows[1]);
    free((void *)row);
    png_destroy_read_struct(&png, &info, NULL);
    return ok;
}

bool book_cover_thumbnail(const uint8_t *data, size_t size, bool png,
                          uint8_t out[BOOK_COVER_W * BOOK_COVER_H]) {
    return book_image_grayscale(data, size, png, BOOK_COVER_W, BOOK_COVER_H, out);
}

bool book_image_grayscale(const uint8_t *data, size_t size, bool png,
                          unsigned out_width, unsigned out_height, uint8_t *out) {
    if (!data || !size || !out || !out_width || !out_height ||
        out_width > 1216 || out_height > 1216 || (size_t)out_width * out_height > 684u * 1216u)
        return false;
    return png ? png_gray_input(data, size, NULL, out_width, out_height, out)
               : jpeg_gray(data, size, out_width, out_height, out);
}



/* ---- 共享封面缓存 / Shared cover cache ---- */

#define COVER_CACHE_MAGIC UINT32_C(0x52435041)
#define COVER_CACHE_SCHEMA 1u

typedef struct {
    uint32_t magic, schema, template_version, width, height;
    uint64_t file_size;
    int64_t modified;
    uint64_t path_hash, text_hash;
} cover_cache_header_t;

static uint64_t cover_hash(uint64_t hash, const char *text) {
    if (!text) text = "";
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    return (hash ^ 0xffu) * UINT64_C(1099511628211);
}

static bool cover_cache_key(const char *source, const char *title, const char *author,
                            char *path, size_t cap, cover_cache_header_t *header) {
    struct stat st;
    if (stat(source, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) return false;
    uint64_t path_hash = cover_hash(UINT64_C(14695981039346656037), source);
    uint64_t text_hash = cover_hash(cover_hash(UINT64_C(14695981039346656037), title), author);
    *header = (cover_cache_header_t){
        .magic = COVER_CACHE_MAGIC, .schema = COVER_CACHE_SCHEMA,
        .template_version = BOOK_AUTO_COVER_VERSION,
        .width = BOOK_COVER_W, .height = BOOK_COVER_H,
        .file_size = (uint64_t)st.st_size, .modified = (int64_t)st.st_mtime,
        .path_hash = path_hash, .text_hash = text_hash,
    };
    return snprintf(path, cap, "/sdcard/.readpico/covers/%016llx-%ux%u.bin",
                    (unsigned long long)path_hash, BOOK_COVER_W, BOOK_COVER_H) < (int)cap;
}

static bool cover_cache_read(const char *path, const cover_cache_header_t *expected,
                             uint8_t out[BOOK_COVER_W * BOOK_COVER_H]) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    cover_cache_header_t found;
    bool ok = fread(&found, 1, sizeof(found), file) == sizeof(found) &&
              found.magic == expected->magic && found.schema == expected->schema &&
              found.template_version == expected->template_version &&
              found.width == expected->width && found.height == expected->height &&
              found.file_size == expected->file_size && found.modified == expected->modified &&
              found.path_hash == expected->path_hash && found.text_hash == expected->text_hash &&
              fread(out, 1, BOOK_COVER_W * BOOK_COVER_H, file) == BOOK_COVER_W * BOOK_COVER_H &&
              fgetc(file) == EOF;
    fclose(file);
    return ok;
}

static void cover_cache_write(const char *path, const cover_cache_header_t *header,
                              const uint8_t out[BOOK_COVER_W * BOOK_COVER_H]) {
    if (mkdir("/sdcard/.readpico", 0777) && errno != EEXIST) return;
    if (mkdir("/sdcard/.readpico/covers", 0777) && errno != EEXIST) return;
    char temp[128];
    if (snprintf(temp, sizeof(temp), "%s.tmp", path) >= (int)sizeof(temp)) return;
    FILE *file = fopen(temp, "wb");
    if (!file) return;
    bool ok = fwrite(header, 1, sizeof(*header), file) == sizeof(*header) &&
              fwrite(out, 1, BOOK_COVER_W * BOOK_COVER_H, file) == BOOK_COVER_W * BOOK_COVER_H;
    if (fclose(file)) ok = false;
    if (ok) {
        remove(path);
        if (!rename(temp, path)) return;
    }
    remove(temp);
}

static bool book_cover_load_gray_impl(const char *path, const char *title, const char *author,
                          uint8_t out[BOOK_COVER_W * BOOK_COVER_H],
                          bool allow_decode, bool *pending, bool allow_auto, bool *auto_cache) {
    if (pending) *pending = false;
    if (!path || !*path || !out) return false;
    char canonical_title[256];
    if (book_title_from_path(path, canonical_title, sizeof(canonical_title))) title = canonical_title;
    else if (!title || !*title) return false;
    char cache_path[120];
    cover_cache_header_t key;
    bool cacheable = cover_cache_key(path, title, author, cache_path, sizeof(cache_path), &key);
    if (!cacheable) return false;
    if (cover_cache_read(cache_path, &key, out)) return true;
    if (!allow_decode) { if (pending) *pending = true; return false; }

    bool good = false;
    bool cache_result = true;
    const char *ext = strrchr(path, '.');
    if (ext && !strcasecmp(ext, ".epub")) {
        uint8_t *data = NULL;
        size_t size = 0;
        bool png = false;
        size_t free_bytes=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        size_t budget=free_bytes>2u*1024u*1024u?free_bytes-2u*1024u*1024u:0;
        esp_err_t err = budget ? book_epub_cover_bounded(path,&data,&size,&png,budget) : ESP_ERR_NO_MEM;
        if (err == ESP_OK) good = book_cover_thumbnail(data, size, png, out);
        free(data);
        if (err == ESP_ERR_NO_MEM) cache_result = false;
    }
    if (!good && !allow_auto) { if(pending)*pending=true;if(auto_cache)*auto_cache=cache_result;return false; }
    if (!good) good = book_auto_cover_render(path, title, author, BOOK_COVER_W, BOOK_COVER_H, out);
    if (good && cache_result) cover_cache_write(cache_path, &key, out);
    return good;
}

// 与网格缓存分离，不能把已经裁剪的封面当作完整原图。
// Separate from grid thumbnails: a cropped cache cannot represent the complete source.
static bool book_cover_load_list_gray_impl(const char *path, const char *title, const char *author,
                               uint8_t out[BOOK_COVER_W * BOOK_COVER_H], bool allow_decode,
                               bool *pending, unsigned *width, unsigned *height, bool allow_auto, bool *auto_cache) {
    if (pending) *pending = false;
    if (!path || !out || !width || !height) return false;
    *width = BOOK_COVER_W; *height = BOOK_COVER_H;
    char canonical[256], cache_path[120];
    if (book_title_from_path(path, canonical, sizeof(canonical))) title = canonical;
    if (!title || !*title) return false;
    cover_cache_header_t key;
    if (!cover_cache_key(path, title, author, cache_path, sizeof(cache_path), &key)) return false;
    char *suffix = strrchr(cache_path, '.');
    if (!suffix || (size_t)(suffix-cache_path)+10 >= sizeof(cache_path)) return false;
    strcpy(suffix, "-list.bin");
    FILE *file = fopen(cache_path, "rb");
    if (file) {
        cover_cache_header_t found;
        bool good = fread(&found, 1, sizeof(found), file) == sizeof(found) &&
            found.magic == key.magic && found.schema == key.schema &&
            found.template_version == key.template_version && found.file_size == key.file_size &&
            found.modified == key.modified && found.path_hash == key.path_hash && found.text_hash == key.text_hash &&
            found.width && found.height && found.width <= BOOK_COVER_W && found.height <= BOOK_COVER_H;
        size_t bytes = good ? (size_t)found.width * found.height : 0;
        if (good) good = fread(out, 1, bytes, file) == bytes && fgetc(file) == EOF;
        fclose(file);
        if (good) { *width = found.width; *height = found.height; return true; }
    }
    if (!allow_decode) { if (pending) *pending = true; return false; }
    bool good = false, cacheable = true;
    const char *ext = strrchr(path, '.');
    if (ext && !strcasecmp(ext, ".epub")) {
        uint8_t *data = NULL; size_t size = 0; bool png = false;
        size_t available = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        size_t budget = available > 1536u*1024u ? available - 1536u*1024u : 0;
        if (budget > 4u*1024u*1024u) budget = 4u*1024u*1024u;
        esp_err_t error = budget ? book_epub_cover_bounded(path, &data, &size, &png, budget) : ESP_ERR_NO_MEM;
        unsigned sw = 0, sh = 0;
        if (error == ESP_OK && book_image_dimensions(data, size, png, &sw, &sh) && sw && sh) {
            if ((uint64_t)sw*BOOK_COVER_H > (uint64_t)sh*BOOK_COVER_W) {
                *width = BOOK_COVER_W; *height = ((uint64_t)sh*BOOK_COVER_W + sw/2)/sw;
            } else { *height = BOOK_COVER_H; *width = ((uint64_t)sw*BOOK_COVER_H + sh/2)/sh; }
            if (!*width) *width = 1;
            if (!*height) *height = 1;
            good = book_image_grayscale(data, size, png, *width, *height, out);
        }
        free(data);
        if ((error != ESP_OK && error != ESP_ERR_NOT_FOUND) || (error == ESP_OK && !good)) cacheable = false;
    }
    if (!good && !allow_auto) { if(pending)*pending=true;if(auto_cache)*auto_cache=cacheable;return false; }
    if (!good) {
        *width = BOOK_COVER_W; *height = BOOK_COVER_H;
        good = book_auto_cover_render(path, title, author, *width, *height, out);
    }
    if (good && cacheable) {
        // 有效尺寸和像素一起原子落盘；最多一张小图，无整页解码工作区。
        // Persist dimensions and pixels atomically; one small image, never a whole-page decode workspace.
        key.width = *width; key.height = *height;
        char temp[128];
        if ((!mkdir("/sdcard/.readpico",0777) || errno==EEXIST) &&
            (!mkdir("/sdcard/.readpico/covers",0777) || errno==EEXIST) &&
            snprintf(temp,sizeof(temp),"%s.tmp",cache_path)<(int)sizeof(temp)) {
            file = fopen(temp,"wb");
            if (file) {
                size_t bytes = (size_t)*width * *height;
                bool saved = fwrite(&key,1,sizeof(key),file)==sizeof(key) && fwrite(out,1,bytes,file)==bytes;
                if (fclose(file)) saved = false;
                if (!saved || rename(temp,cache_path)) (void)remove(temp);
            }
        }
    }
    return good;
}

/* ---- 已处理的黑白点阵落卡 / Persistent finished monochrome raster ---- */
// 更改封面点阵算法时递增此版本；首页与书架各保留一个快刷结果，不按相位累积文件。
// Bump for changes to dithering; retain one Home and one shelf raster per book, not one per phase.
// gen22：push.txt 的点阵输出随专属版式改变，落卡快刷结果同步失效重建。
// gen22: the push book's raster changed with its dedicated template; drop stale
// fast-cover rasters too.
#define FAST_COVER_REVISION 2u
typedef struct {
    cover_cache_header_t source;
    uint32_t revision, width, height, stride, contrast, phase, white_edge;
    uint64_t checksum;
} fast_cover_header_t;
static uint64_t fast_cover_checksum(const uint8_t *data, size_t size) {
    uint64_t hash = UINT64_C(14695981039346656037);
    while (size--) hash = (hash ^ *data++) * UINT64_C(1099511628211);
    return hash;
}
static bool fast_cover_key(const char *source, const char *title, const char *author,
                            book_fast_cover_key_t key, char path[120], fast_cover_header_t *header) {
    const bool home = key.width == 172 && key.height == 250;
    if (!source || !*source || !key.width || key.width > 176 || !key.height ||
        (key.height > 240 && !home) ||
        key.stride < (key.width+7)/8 || key.stride > 22 || key.phase > 15 || key.contrast > 200) return false;
    char canonical[256];
    if (book_title_from_path(source, canonical, sizeof(canonical))) title = canonical;
    if (!title || !*title) return false;
    memset(header, 0, sizeof(*header));
    if (!cover_cache_key(source, title, author, path, 120, &header->source)) return false;
    snprintf(path, 120, "/sdcard/.readpico/covers/%016llx-%sfast.bin",
             (unsigned long long)header->source.path_hash, home ? "home-" : "");
    header->revision = FAST_COVER_REVISION; header->width = key.width; header->height = key.height;
    header->stride = key.stride; header->contrast = key.contrast; header->phase = key.phase;
    return true;
}
bool book_cover_fast_cache_read(const char *source, const char *title, const char *author,
                                book_fast_cover_key_t key, uint8_t *out, bool *white_edge) {
    char path[120]; fast_cover_header_t expected, found;
    if (!out || !white_edge || !fast_cover_key(source, title, author, key, path, &expected)) return false;
    FILE *file = fopen(path, "rb"); if (!file) return false;
    const size_t bytes = (size_t)key.stride * key.height;
    bool ok = fread(&found, 1, sizeof(found), file) == sizeof(found) &&
        found.source.magic == expected.source.magic && found.source.schema == expected.source.schema &&
        found.source.template_version == expected.source.template_version &&
        found.source.file_size == expected.source.file_size && found.source.modified == expected.source.modified &&
        found.source.path_hash == expected.source.path_hash && found.source.text_hash == expected.source.text_hash &&
        found.revision == expected.revision && found.width == key.width && found.height == key.height &&
        found.stride == key.stride && found.contrast == key.contrast && found.phase == key.phase &&
        found.white_edge <= 1 && fread(out, 1, bytes, file) == bytes && fgetc(file) == EOF &&
        fast_cover_checksum(out, bytes) == found.checksum;
    fclose(file);
    if (ok) *white_edge = found.white_edge != 0;
    return ok;
}
static void fast_cache_write(const char *source,const char *title,const char *author,
                             book_fast_cover_key_t key,const uint8_t *out,bool white_edge,int64_t size,int64_t modified) {
    char path[120], temp[128]; fast_cover_header_t header;
    if (!out || !fast_cover_key(source, title, author, key, path, &header)) return;
    if(size>=0&&(header.source.file_size!=(uint64_t)size||header.source.modified!=modified))return;
    if ((mkdir("/sdcard/.readpico", 0777) && errno != EEXIST) ||
        (mkdir("/sdcard/.readpico/covers", 0777) && errno != EEXIST)) return;
    if (snprintf(temp, sizeof(temp), "%s.tmp", path) >= (int)sizeof(temp)) return;
    header.white_edge = white_edge;
    const size_t bytes = (size_t)key.stride * key.height;
    header.checksum = fast_cover_checksum(out, bytes);
    FILE *file = fopen(temp, "wb"); if (!file) return;
    bool ok = fwrite(&header, 1, sizeof(header), file) == sizeof(header) && fwrite(out, 1, bytes, file) == bytes;
    if (fclose(file)) ok = false;
    if (ok && !rename(temp, path)) return;
    (void)remove(temp);
}

static bool file_frame(FILE *file, bool png, uint8_t *sof, unsigned *width, unsigned *height) {
    if (fseek(file, 0, SEEK_SET)) return false;
    if (png) {
        uint8_t header[24];
        return fread(header, 1, sizeof(header), file) == sizeof(header) && book_image_dimensions(header, sizeof(header), true, width, height);
    }
    if (fgetc(file) != 0xff || fgetc(file) != 0xd8) return false;
    for (unsigned segment = 0; segment < 4096; ++segment) {
        int first = fgetc(file);
        if (first == EOF) return false;
        if (first != 0xff) continue;
        int marker;
        do { marker = fgetc(file); } while (marker == 0xff);
        if (marker == EOF || marker == 0xda || marker == 0xd9) return false;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd8)) continue;
        int high = fgetc(file), low = fgetc(file);
        if (high < 0 || low < 0) return false;
        unsigned length = (unsigned)high * 256 + (unsigned)low;
        if (length < 2) return false;
        if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
            uint8_t frame[5];
            if (length < 7 || fread(frame, 1, sizeof(frame), file) != sizeof(frame)) return false;
            *height = (unsigned)frame[1] * 256 + frame[2];
            *width = (unsigned)frame[3] * 256 + frame[4];
            if (sof) *sof = (uint8_t)marker;
            return *width && *height && *width <= 8192 && *height <= 8192 && frame[0] == 8;
        }
        if (fseek(file, (long)length - 2, SEEK_CUR)) return false;
    }
    return false;
}
bool book_image_file_dimensions(const char *path, bool png, unsigned *width, unsigned *height) {
    if (!path || !width || !height) return false;
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || (uint64_t)st.st_size > BOOK_IMAGE_FILE_MAX) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    *width = *height = 0;
    bool ok = file_frame(file, png, NULL, width, height);
    fclose(file);
    return ok;
}
typedef struct {
    uint8_t *out;
    unsigned width, height, source_w, source_h;
    size_t pixels;
    unsigned blocks;
    FILE *file;
} jpeg_file_plane_t;
static int jpeg_file_collect(JPEGDRAW *draw) {
    jpeg_file_plane_t *plane = draw->pUser;
    if (!plane || !draw->pPixels || draw->iBpp != 8 || draw->iWidth <= 0 || draw->iHeight <= 0 || draw->x < 0 || draw->y < 0) return 0;
    book_crop_t crop = book_cover_crop(plane->source_w, plane->source_h, plane->width, plane->height);
    const uint8_t *block = (const uint8_t *)draw->pPixels;
    // 只绘制映射到当前 MCU 的目标采样点；输出最多一屏，不创建源图平面。
    // Sample only destination pixels mapped into this MCU, without a source-sized plane.
    decode_yield(++plane->blocks);
    unsigned y0 = (unsigned)draw->y > crop.y ? ((uint64_t)(draw->y - crop.y) * plane->height + crop.height - 1) / crop.height : 0;
    unsigned y1 = (unsigned)(draw->y + draw->iHeight) > crop.y ? ((uint64_t)(draw->y + draw->iHeight - crop.y) * plane->height + crop.height - 1) / crop.height : 0;
    unsigned x0 = (unsigned)draw->x > crop.x ? ((uint64_t)(draw->x - crop.x) * plane->width + crop.width - 1) / crop.width : 0;
    unsigned x1 = (unsigned)(draw->x + draw->iWidthUsed) > crop.x ? ((uint64_t)(draw->x + draw->iWidthUsed - crop.x) * plane->width + crop.width - 1) / crop.width : 0;
    if (y1 > plane->height) y1 = plane->height;
    if (x1 > plane->width) x1 = plane->width;
    for (unsigned y = y0; y < y1; ++y) {
        unsigned sy = crop.y + (uint64_t)y * crop.height / plane->height;
        if (sy < (unsigned)draw->y || sy >= (unsigned)(draw->y + draw->iHeight)) continue;
        for (unsigned x = x0; x < x1; ++x) {
            unsigned sx = crop.x + (uint64_t)x * crop.width / plane->width;
            if (sx < (unsigned)draw->x || sx >= (unsigned)(draw->x + draw->iWidthUsed)) continue;
            plane->out[(size_t)y * plane->width + x] = block[(size_t)(sy - draw->y) * draw->iWidth + sx - draw->x];
            ++plane->pixels;
        }
    }
    return 1;
}
static stream_jpeg_size_t jpeg_stream_read(JDEC *decoder, uint8_t *bytes, stream_jpeg_size_t count) {
    jpeg_file_plane_t *plane = decoder->device;
    if (!plane || !plane->file) return 0;
    if (!bytes) return fseek(plane->file, (long)count, SEEK_CUR) == 0 ? count : 0;
    return (stream_jpeg_size_t)fread(bytes, 1, count, plane->file);
}
static stream_jpeg_result_t jpeg_stream_collect(JDEC *decoder, void *bitmap, JRECT *rect) {
    jpeg_file_plane_t *plane = decoder->device;
    unsigned width = rect->right - rect->left + 1, height = rect->bottom - rect->top + 1;
    if (!width || !height || width * height > 256) return 0;
    uint8_t gray[256];
    const uint8_t *rgb = bitmap;
    for (unsigned i = 0; i < width * height; ++i) gray[i] = luminance(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
    JPEGDRAW draw = {.x = rect->left, .y = rect->top, .iWidth = (int)width, .iHeight = (int)height,
        .iWidthUsed = (int)width, .iBpp = 8, .pPixels = (uint16_t *)gray, .pUser = plane};
    return (stream_jpeg_result_t)jpeg_file_collect(&draw);
}
static bool jpeg_baseline_file(FILE *file, unsigned source_w, unsigned source_h,
                               unsigned width, unsigned height, jpeg_file_plane_t *plane) {
    unsigned scale = 0;
    while (scale < 3 && source_w / (2u << scale) >= width && source_h / (2u << scale) >= height) ++scale;
    plane->file = file; plane->source_w = source_w >> scale; plane->source_h = source_h >> scale;
    void *workspace = cover_alloc(4096);
    if (!workspace) return false;
    JDEC decoder;
    bool ok = jd_prepare(&decoder, jpeg_stream_read, workspace, 4096, plane) == JDR_OK &&
              jd_decomp(&decoder, jpeg_stream_collect, (uint8_t)scale) == JDR_OK;
    free(workspace);
    return ok;
}
extern int jpegdec_gray_file(FILE *, int, unsigned, unsigned, unsigned, unsigned, bool,
                             JPEG_DRAW_CALLBACK *, void *, unsigned *, unsigned *);
bool book_image_file_grayscale(const char *path, bool png, unsigned width, unsigned height, uint8_t *out) {
    if (!path || !out || !width || !height || width > 1216 || height > 1216 || (uint64_t)width * height > 684u * 1216u) return false;
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || (uint64_t)st.st_size > BOOK_IMAGE_FILE_MAX) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    bool ok = false;
    if (png) ok = png_gray_input(NULL, (size_t)st.st_size, file, width, height, out);
    else {
        uint8_t sof = 0;
        unsigned source_w = 0, source_h = 0;
        if (file_frame(file, false, &sof, &source_w, &source_h) && !fseek(file, 0, SEEK_SET)) {
            jpeg_file_plane_t plane = {.out = out, .width = width, .height = height};
            memset(out, 255, (size_t)width * height);
            if (sof == 0xc2)
                ok = jpegdec_gray_file(file, (int)st.st_size, source_w, source_h, width, height, true,
                    jpeg_file_collect, &plane, &plane.source_w, &plane.source_h) != 0;
            else ok = jpeg_baseline_file(file, source_w, source_h, width, height, &plane);
            ok = ok && plane.pixels >= (size_t)width * height;
        }
    }
    fclose(file);
    return ok;
}

bool book_cover_load_gray(const char *p,const char *t,const char *a,uint8_t out[BOOK_COVER_W * BOOK_COVER_H],bool decode,bool *pending) {
    return book_cover_load_gray_impl(p,t,a,out,decode,pending,true,NULL);
}
bool book_cover_load_list_gray(const char *p,const char *t,const char *a,uint8_t out[BOOK_COVER_W * BOOK_COVER_H],bool decode,bool *pending,unsigned *w,unsigned *h) {
    return book_cover_load_list_gray_impl(p,t,a,out,decode,pending,w,h,true,NULL);
}
int book_cover_load_io(const char *p,const char *t,const char *a,uint8_t *out,bool list,unsigned *w,unsigned *h,bool *auto_cache) {
    bool automatic=false;*w=BOOK_COVER_W;*h=BOOK_COVER_H;
    bool ready=list?book_cover_load_list_gray_impl(p,t,a,out,true,&automatic,w,h,false,auto_cache):
                    book_cover_load_gray_impl(p,t,a,out,true,&automatic,false,auto_cache);
    return ready?1:automatic?0:-1;
}
bool book_cover_list_cache_dimensions(const char *p,const char *t,const char *a,unsigned *w,unsigned *h) {
    char canonical[256],path[120];cover_cache_header_t key,found;
    if(book_title_from_path(p,canonical,sizeof(canonical)))t=canonical;
    if(!cover_cache_key(p,t,a,path,sizeof(path),&key))return false;
    char *suffix=strrchr(path,'.');if(!suffix)return false;strcpy(suffix,"-list.bin");
    FILE *f=fopen(path,"rb");if(!f)return false;
    bool ok=fread(&found,1,sizeof(found),f)==sizeof(found)&&found.magic==key.magic&&found.schema==key.schema&&
        found.template_version==key.template_version&&found.file_size==key.file_size&&found.modified==key.modified&&
        found.path_hash==key.path_hash&&found.text_hash==key.text_hash&&found.width&&found.height&&
        found.width<=BOOK_COVER_W&&found.height<=BOOK_COVER_H;
    fclose(f);if(ok){*w=found.width;*h=found.height;}return ok;
}
void book_cover_store_generated(const char *p,const char *t,const char *a,const uint8_t *pixels,bool list,int64_t size,int64_t modified) {
    char canonical[256],path[120];cover_cache_header_t key;
    if(book_title_from_path(p,canonical,sizeof(canonical)))t=canonical;
    if(!cover_cache_key(p,t,a,path,sizeof(path),&key))return;
    if(key.file_size!=(uint64_t)size||key.modified!=modified)return;
    if(list){char *suffix=strrchr(path,'.');if(!suffix)return;strcpy(suffix,"-list.bin");}
    cover_cache_write(path,&key,pixels);
}

void book_cover_fast_cache_write(const char *p,const char *t,const char *a,book_fast_cover_key_t key,const uint8_t *pixels,bool edge) {
    fast_cache_write(p,t,a,key,pixels,edge,-1,0);
}
void book_cover_fast_cache_write_checked(const char *p,const char *t,const char *a,book_fast_cover_key_t key,const uint8_t *pixels,bool edge,int64_t size,int64_t modified) {
    fast_cache_write(p,t,a,key,pixels,edge,size,modified);
}
