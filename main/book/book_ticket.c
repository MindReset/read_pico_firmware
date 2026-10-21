/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：锁屏票根从持久进度与真实阅读计数生成；没有可靠时钟时不编造今日数据。
 * 票面上下以裁纸缺口绘制，最近阅读的 EPUB 封面作为灰阶背景。
 * English: Build the ticket from persisted progress and measured reading data; omit daily data without a valid clock.
 * Scalloped paper edges sit over a grayscale wallpaper made from the latest EPUB cover.
 * 用户修订：锁屏入口统一系统字体，截短测量须与实际界面字体及字号一致。
 * User revision: lock entry selects the system face; truncation must measure the actual UI face and size.
 */
#include "book_ticket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include "book_cover.h"
#include "book_epub.h"
#include "book_progress.h"
#include "book_title.h"
#include "epdiy.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "read_pico_pmu.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_image_dither.h"

#define TICKET_MAGIC UINT32_C(0x52505431)
typedef struct {
    uint32_t magic, day;
    uint64_t total_seconds, today_seconds;
    uint32_t total_turns, today_turns;
} ticket_stats_t;
#define TICKET_HEAT_MAGIC UINT32_C(0x52504831)
typedef struct {
    uint32_t magic;
    uint32_t day[30];
    uint32_t seconds[30];
} ticket_heat_t;
#define TICKET_MONTH_MAGIC UINT32_C(0x52504832)
typedef struct { uint32_t magic, day[32], seconds[32]; } ticket_month_t;
static void month_load(nvs_handle_t h, ticket_month_t *out) {
    size_t len = sizeof(*out);
    if (nvs_get_blob(h, "heat32", out, &len) == ESP_OK && len == sizeof(*out) && out->magic == TICKET_MONTH_MAGIC) return;
    memset(out, 0, sizeof(*out)); out->magic = TICKET_MONTH_MAGIC;
    ticket_heat_t old = {0}; len = sizeof(old);
    if (nvs_get_blob(h, "heatmap", &old, &len) != ESP_OK || len != sizeof(old) || old.magic != TICKET_HEAT_MAGIC) return;
    for (unsigned i = 0; i < 30; ++i) if (old.day[i] && old.day[i] % 30 == i) {
        unsigned slot = old.day[i] % 32;
        if (out->day[slot] < old.day[i]) { out->day[slot] = old.day[i]; out->seconds[slot] = old.seconds[i]; }
    }
}
static uint32_t local_day(void) {
    time_t now = time(NULL);
    if (now < 1704067200) {
        const pmu_snapshot_t *pmu = read_pico_pmu_get();
        if (pmu && pmu->time_synced && pmu->unix_sec >= 1704067200) now = pmu->unix_sec;
    }
    if (now < 1704067200) return 0;
    return (uint32_t)((now + 8 * 3600) / 86400);
}
static void stats_load(ticket_stats_t *out) {
    memset(out, 0, sizeof(*out));
    nvs_handle_t h;
    if (nvs_open("rp_ticket", NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(*out);
    esp_err_t err = nvs_get_blob(h, "stats", out, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(*out) || out->magic != TICKET_MAGIC) memset(out, 0, sizeof(*out));
}
esp_err_t book_ticket_record(uint32_t seconds, uint32_t turns) {
    if (!seconds && !turns) return ESP_OK;
    ticket_stats_t stats; stats_load(&stats);
    stats.magic = TICKET_MAGIC;
    uint32_t day = local_day();
    if (day && stats.day != day) { stats.day = day; stats.today_seconds = 0; stats.today_turns = 0; }
    stats.total_seconds += seconds;
    stats.total_turns += turns;
    if (day) { stats.today_seconds += seconds; stats.today_turns += turns; }
    nvs_handle_t h;
    esp_err_t err = nvs_open("rp_ticket", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "stats", &stats, sizeof(stats));
    if (err == ESP_OK && day && seconds) {
        // 先迁移旧环，再同时维护两个环，保留降级兼容。/ Migrate before updating both rings, preserving downgrade compatibility.
        ticket_month_t month; month_load(h, &month);
        unsigned mslot = day % 32;
        if (month.day[mslot] != day) { month.day[mslot] = day; month.seconds[mslot] = 0; }
        uint32_t previous = month.seconds[mslot];
        month.seconds[mslot] = UINT32_MAX - previous < seconds ? UINT32_MAX : previous + seconds;
        err = nvs_set_blob(h, "heat32", &month, sizeof(month));
        ticket_heat_t heat = {0};
        size_t len = sizeof(heat);
        if (nvs_get_blob(h, "heatmap", &heat, &len) != ESP_OK ||
            len != sizeof(heat) || heat.magic != TICKET_HEAT_MAGIC) {
            memset(&heat, 0, sizeof(heat));
            heat.magic = TICKET_HEAT_MAGIC;
        }
        unsigned slot = day % 30;
        if (heat.day[slot] != day) { heat.day[slot] = day; heat.seconds[slot] = 0; }
        uint32_t old = heat.seconds[slot];
        heat.seconds[slot] = UINT32_MAX - old < seconds ? UINT32_MAX : old + seconds;
        if (err == ESP_OK) err = nvs_set_blob(h, "heatmap", &heat, sizeof(heat));
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
bool book_ticket_recent_days(uint32_t seconds[30]) {
    if (!seconds) return false;
    memset(seconds, 0, 30 * sizeof(*seconds));
    uint32_t today = local_day();
    if (!today) return false;
    nvs_handle_t h;
    if (nvs_open("rp_ticket", NVS_READONLY, &h) != ESP_OK) return true;
    ticket_heat_t heat = {0};
    size_t len = sizeof(heat);
    esp_err_t err = nvs_get_blob(h, "heatmap", &heat, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(heat) || heat.magic != TICKET_HEAT_MAGIC) return true;
    for (unsigned i = 0; i < 30; ++i) {
        uint32_t day = today - (29 - i);
        unsigned slot = day % 30;
        if (heat.day[slot] == day) seconds[i] = heat.seconds[slot];
    }
    return true;
}
bool book_ticket_month_days(int year, int month, uint32_t seconds[31]) {
    if (!seconds) return false;
    memset(seconds, 0, 31*sizeof(*seconds));
    uint32_t today = local_day();
    if (!today || year < 2024 || year > 9999 || month < 1 || month > 12) return false;
    time_t timestamp = (time_t)today * 86400;
    struct tm date = {0}; gmtime_r(&timestamp, &date);
    if (date.tm_year+1900 != year || date.tm_mon+1 != month) return false;
    nvs_handle_t h;
    if (nvs_open("rp_ticket", NVS_READONLY, &h) != ESP_OK) return true;
    ticket_month_t heat; month_load(h, &heat); nvs_close(h);
    uint32_t first = today-(date.tm_mday-1);
    for (int i=0; i<date.tm_mday; ++i) {
        uint32_t day=first+i; unsigned slot=day%32;
        if (heat.day[slot]==day) seconds[i]=heat.seconds[slot];
    }
    return true;
}
static void text_fit(char *text, int px, int width) {
    while (*text && ui_text_fixed_width_px(ui_text_effective_px(px), text) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}
static uint8_t *cover_load(const char *path, const char *title, const char *author) {
    uint8_t *gray = heap_caps_malloc(BOOK_COVER_W * BOOK_COVER_H,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // The lock screen must not decode a potentially large EPUB while the
    // power key is being handled; use the shelf's cover cache when available.
    // 处理电源键时只读书架封面缓存，不在锁屏路径解码大 EPUB。
    if (gray && !book_cover_load_gray(path, title, author, gray, false, NULL)) {
        free(gray);
        gray = NULL;
    }
    return gray;
}
static void cover_background_draw(uint8_t *fb, const uint8_t *gray) {
    if (!gray) {
        ui_fill_rect_fast((EpdRect){0, 0, UI_LOCK_WIDTH, UI_LOCK_HEIGHT}, 0xb0, fb);
        return;
    }
    int crop_x = 0, crop_y = 0, crop_w = BOOK_COVER_W, crop_h = BOOK_COVER_H;
    if ((int64_t)BOOK_COVER_W * UI_LOCK_HEIGHT > (int64_t)BOOK_COVER_H * UI_LOCK_WIDTH) {
        crop_w = BOOK_COVER_H * UI_LOCK_WIDTH / UI_LOCK_HEIGHT;
        crop_x = (BOOK_COVER_W - crop_w) / 2;
    } else {
        crop_h = BOOK_COVER_W * UI_LOCK_HEIGHT / UI_LOCK_WIDTH;
        crop_y = (BOOK_COVER_H - crop_h) / 2;
    }
    for (int y = 0; y < UI_LOCK_HEIGHT; ++y) {
        const int sy = crop_y + y * crop_h / UI_LOCK_HEIGHT;
        for (int x = 0; x < UI_LOCK_WIDTH; ++x) {
            const int sx = crop_x + x * crop_w / UI_LOCK_WIDTH;
            const uint8_t tone = ui_contrast_gray(gray[sy * BOOK_COVER_W + sx]);
            epd_draw_pixel(x, y, ui_image_dither_gray(tone, x, y), fb);
        }
    }
}
static int notch_depth(int x) {
    const int radius = 18, first = 80, step = 64;
    int dx = (x - first) % step;
    if (dx < 0) dx += step;
    if (dx > step / 2) dx -= step;
    if (dx < -radius || dx > radius) return 0;
    return radius - dx * dx / radius;
}
static void ticket_paper_draw(uint8_t *fb) {
    const int left = 48, right = 636, top = 58, bottom = 1158;
    for (int x = left; x < right; ++x) {
        int cut = notch_depth(x);
        int y0 = top + cut, y1 = bottom - cut;
        ui_fill_rect_fast((EpdRect){x, y0, 1, y1 - y0}, UI_GRAY_WHITE, fb);
        epd_draw_pixel(x, y0, UI_GRAY_BLACK, fb);
        epd_draw_pixel(x, y1 - 1, UI_GRAY_BLACK, fb);
    }
    ui_fill_rect_fast((EpdRect){left, top + 20, 1, bottom - top - 40}, UI_GRAY_BLACK, fb);
    ui_fill_rect_fast((EpdRect){right - 1, top + 20, 1, bottom - top - 40}, UI_GRAY_BLACK, fb);
}
static void ticket_shadow_draw(uint8_t *fb) {
    const int left = 48, right = 636, top = 58, bottom = 1158;
    for (int x = left; x < right; ++x) {
        int cut = notch_depth(x);
        int y0 = top + cut + 8, y1 = bottom - cut + 8;
        ui_fill_rect_fast((EpdRect){x + 5, y0, 1, y1 - y0}, 0x80, fb);
    }
}
static void cover_draw(uint8_t *fb, const uint8_t *gray, const char *title) {
    EpdRect box = {216, 378, 252, 344};
    ui_fill_rect_fast(box, UI_GRAY_LIGHT, fb);
    if (gray) {
        for (int y = 0; y < box.height; ++y)
            for (int x = 0; x < box.width; ++x) {
                int sx = x * BOOK_COVER_W / box.width;
                int sy = y * BOOK_COVER_H / box.height;
                const uint8_t tone = ui_contrast_gray(gray[sy * BOOK_COVER_W + sx]);
                epd_draw_pixel(box.x + x, box.y + y,
                               ui_image_dither_gray(tone, box.x + x, box.y + y), fb);
            }
    } else {
        char short_title[96]; snprintf(short_title, sizeof(short_title), "%s", title);
        text_fit(short_title, 32, box.width - 20);
        ui_text_vc(fb, box.x + box.width / 2, box.y + box.height / 2, 32,
                   short_title, EPD_DRAW_ALIGN_CENTER, false);
    }
    ui_draw_round_rect(fb, box, 0, UI_GRAY_BLACK);
}
// Preserve exact counters, shrinking only when needed. Two lines fit long statistics.
static void ticket_stat(uint8_t *fb, int y, int height, int px, const char *text) {
    px = ui_text_effective_px(px);
    if (px > height) px = height;
    char line[128]; snprintf(line, sizeof(line), "%s", text);
    while (px > 18 && ui_text_fixed_width_px(px, line) > 520) --px;
    if (ui_text_fixed_width_px(px, line) <= 520) {
        ui_text_fixed(fb, 82, y, px, line, EPD_DRAW_ALIGN_LEFT, false);
        return;
    }
    char *sep = strstr(line, "  ·  ");
    if (sep && height >= 44) {
        *sep = 0; char *tail = sep + strlen("  ·  ");
        text_fit(line, 18, 520); text_fit(tail, 18, 520);
        ui_text_fixed(fb, 82, y, 18, line, EPD_DRAW_ALIGN_LEFT, false);
        ui_text_fixed(fb, 82, y + height / 2, 18, tail, EPD_DRAW_ALIGN_LEFT, false);
    } else {
        text_fit(line, px, 520);
        ui_text_fixed(fb, 82, y, px, line, EPD_DRAW_ALIGN_LEFT, false);
    }
}
static void ticket_heading(uint8_t *fb,int top,int height,int px,char *text) {
    // 锁屏入口已切系统字体；标题按原生轮廓输出，独立强制AA，测量与绘制用同一字库。
    // Lock entry selects the system face; rasterize native outlines with explicit AA and measure using the same face.
    px=ui_text_effective_px(px);if(px>height-2)px=height-2;
    px=ui_text_title_fit(text,px,520,text);
    ui_text_title_vc(fb,82,top+height/2,px,text,text,EPD_DRAW_ALIGN_LEFT);
}
bool book_ticket_draw(uint8_t *fb, bool reader_background) {
    (void)reader_background;
    char path[288];
    if (!book_progress_last_path(path, sizeof(path))) return false;
    struct stat st;
    bool file_valid = stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0 &&
                      (uint64_t)st.st_size <= UINT32_MAX;
    book_progress_t progress = {0};
    bool progress_valid = file_valid && book_progress_load(path, st.st_size, &progress);
    char title[256] = {0}, author[128] = {0};
    if (!book_title_from_path(path, title, sizeof(title))) snprintf(title, sizeof(title), "未命名");
    const char *ext = strrchr(path, '.');
    if (ext && !strcasecmp(ext, ".epub")) {
        char meta_title[256] = {0};
        (void)book_epub_metadata_cached(path, meta_title, sizeof(meta_title), author, sizeof(author));
    }
    ticket_stats_t stats; stats_load(&stats);
    uint32_t day = local_day();
    uint8_t *gray = cover_load(path, title, author);
    cover_background_draw(fb, gray);
    ticket_shadow_draw(fb);
    ticket_paper_draw(fb);
    ui_text(fb, 82, 98, 28, "kiikoread  /  阅读票根", EPD_DRAW_ALIGN_LEFT, false);
    ui_hairline(fb, 156, 82, 520, UI_GRAY_BLACK);
    ticket_heading(fb,202,64,50,title);
    if (author[0]) ticket_heading(fb,274,44,28,author);
    cover_draw(fb, gray, title);
    free(gray);
    ui_hairline(fb, 768, 82, 520, UI_GRAY_BLACK);
    char line[96];
    if (progress_valid) snprintf(line, sizeof(line), "已读 %u%%  ·  第 %u 章", progress.pct,
                                 (unsigned)progress.chapter + 1);
    else snprintf(line, sizeof(line), "已读 —  ·  章节 —");
    ticket_stat(fb, 802, 54, 34, line);
    EpdRect track = {82, 868, 520, 10};
    ui_fill_rect_fast(track, UI_GRAY_LIGHT, fb);
    track.width = progress_valid ? track.width * progress.pct / 100 : 0;
    if (track.width) ui_fill_rect_fast(track, UI_GRAY_BLACK, fb);
    if (day && stats.day == day) snprintf(line, sizeof(line), "今日阅读 %llu 分钟  ·  翻页 %u 次",
        (unsigned long long)(stats.today_seconds / 60), (unsigned)stats.today_turns);
    else snprintf(line, sizeof(line), "今日阅读 —  ·  日期未同步");
    ticket_stat(fb, 914, 48, 28, line);
    snprintf(line, sizeof(line), "累计阅读 %llu 小时 %llu 分钟  ·  翻页 %u 次",
             (unsigned long long)(stats.total_seconds / 3600),
             (unsigned long long)((stats.total_seconds / 60) % 60), (unsigned)stats.total_turns);
    ticket_stat(fb, 969, 74, 28, line);
    ui_hairline(fb, 1050, 82, 520, UI_GRAY_BLACK);
    ui_text(fb, 82, 1080, 26, "在每一页里，留下时间的凭证", EPD_DRAW_ALIGN_LEFT, false);
    return true;
}
