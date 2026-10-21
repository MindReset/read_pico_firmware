/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 刷屏出口：按模式推屏、HV 轨空闲下电、pclk 欠载回退。
 *
 * Present path: push by mode, drop HV rails on idle, fall back pclk on
 * underrun.
 * 用户继续优化快刷响应：无灰阶条带时按32位并行归一八像素，已为黑白的字不重复写PSRAM；不删相位、不增加缓存或改变清残影和底栏边界。
 * User continues fast responsiveness tuning: normalize eight pixels per 32-bit word when no gray bands exist and avoid rewriting binary PSRAM words; retain phases, cleanup and navigation bounds without extra caches.
 * 用户修订：三种主页模式均固定底栏，仅局部移动横条；普通切页复用柔和灰阶表，黑白端点单向驱动、等灰保持，中灰保留校准相位。取消每三次全像素整理，仍每十二次完整清残影。快刷棋盘格输出、首次进入、解锁及手动全刷不变，不得伪造back。
 * User revision: all main modes retain navigation with local markers; ordinary transitions reuse soft grays with directional BW endpoints, held equal grays and calibrated changed midgrays. Remove third-page full-pixel settling but retain twelfth-page cleanup. Fast checkerboards, initial entry, unlock and manual full output remain; never fabricate back.
 * 用户修订：所有推屏及欠载恢复与 OTA 擦写共用递归锁；无升级任务时保持原波形、范围和节奏。
 * User revision: serialize all display output and underrun recovery with OTA flash operations; keep existing waveforms, bounds and cadence outside updates.
 * 用户要求加快主页水波纹：复用阅读的一拍一带调度，以8带37相共44拍、11090us完成；保留主页原灰阶序列、底栏边界、OTA锁及失败恢复。
 * User requests faster main-page ripple: reuse reader-style compact launches for eight bands/37 phases over 44 ticks at 11090us; retain main gray sequences, navigation bounds, OTA locking and recovery.
 * 用户修订：书架顶部文字、框线与封面/亚克力一起使用水波纹，不再拆分顶部差分刷新；底栏仍固定。
 * User revision: shelf headers, frames, covers and acrylic share one ripple instead of a separate header differential; navigation remains fixed.

 */

#include "display.h"
#include "read_pico_board.h"
#include "ota_online.h"
#include "continuous_du.h"
#include "push_sink.h"

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "e0470_epaper_waveform.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "display_main_waveform.h"
#include "display_reader_waveform.h"
#include "ui/ui_nav_layout.h"

static const char* TAG = "read_pico";
static bool s_bulk_io;
static bool s_full_refresh_requested;
void display_request_full_refresh(void) { s_full_refresh_requested = true; }
static int s_pclk_mhz = DISPLAY_PCLK_DEFAULT_MHZ;

void display_set_bulk_io(bool active) {
    s_bulk_io = active;
    ESP_LOGI(TAG, "bulk I/O scan margin %s", active ? "on" : "off");
}

// 高压轨在连续操作间保留8秒，避免反复500ms放电；像素驱动在每轮结束时立即释放。
// Keep HV rails for an eight-second burst to avoid repeated 500ms discharge; release pixel drive after every output.
#define RAILS_IDLE_TIMEOUT_MS 8000

// 0 表示轨道已断电；否则是到期时间（ms），到点后主循环断电。
// 0 means the rails are off; otherwise a deadline (ms) after which the loop powers them down.
static int64_t rails_deadline_ms;
static int64_t drive_release_retry_ms;

void rails_keepalive(void) {
    bool held = pico_online_display_begin();
    const int64_t now_ms = esp_timer_get_time() / 1000;
    // 包括无变化和失败返回：释放像素驱动，不增加扫描或全刷。
    // Release pixel drive even after unchanged/failed output, without another scan or full refresh.
    drive_release_retry_ms = read_pico_epd_release_drive() ? 0 : now_ms + 50;
    rails_deadline_ms = now_ms + RAILS_IDLE_TIMEOUT_MS;
    pico_online_display_end(held);
}

void rails_idle_check(int64_t now_ms) {
    if (!(drive_release_retry_ms && now_ms >= drive_release_retry_ms) &&
        !(rails_deadline_ms && now_ms >= rails_deadline_ms)) return;
    // 和实际推屏共用递归锁，不能在另一轮扫描中途释放驱动。
    // Share the scan's recursive lock; never release drive during another output.
    bool held = pico_online_display_begin();
    // 短暂 I2C 故障按50ms重试；成功后不再访问总线，也不重画页面。
    // Retry a transient I2C failure at 50ms intervals; stop bus access on success without repainting.
    if (drive_release_retry_ms && now_ms >= drive_release_retry_ms)
        drive_release_retry_ms = read_pico_epd_release_drive() ? 0 : now_ms + 50;
    if (rails_deadline_ms != 0 && now_ms >= rails_deadline_ms) {
        rails_deadline_ms = 0;
        drive_release_retry_ms = 0;
        epd_poweroff();
    }
    pico_online_display_end(held);
}

// 20 相完整/原厂 DU 按厂家时序走 FULL；只有触摸笔迹用的 8 帧跟随 DU 走 FAST。
// 20-phase full / vendor DU uses FULL timing; the 8-frame FOLLOW DU used for ink trails uses FAST.
static void use_scan_for(const EpdWaveform* waveform, enum EpdDrawMode mode) {
    const bool fast = (mode & 0xF) == MODE_DU && waveform == &E0470_FOLLOW_WAVEFORM;
    read_pico_epd_use_scan(fast ? READ_PICO_EPD_SCAN_FAST : READ_PICO_EPD_SCAN_FULL);
    // 高层刷新保持整屏扫描；两条63行可用队列在第127行入队前启动。
    // High-level updates scan the full panel; two 63-slot queues start before line 127 is enqueued.
    epd_lcd_set_prefill_lines(s_bulk_io ? 127 : (fast ? 64 : 32));
}

// 自上次整屏 GC16 以来的整页差分次数。局部反馈不计数，否则菜单按压几次就会触发黑白全刷。
// Whole-page differentials since the last full GC16. Local feedback is excluded so a few taps cannot trigger flashing.
static int s_page_refreshes;

static const void* s_main_armed;
static bool s_main_fault;
static bool s_main_shelf_page;
static EpdRect s_main_bands[3];
static unsigned s_main_band_count;
static bool s_main_shelf_exit;
static bool s_main_changing_page;
_Static_assert((UI_NAV_REFRESH_END & 31) == 0, "fixed navigation begins on an aligned framebuffer column");
_Static_assert(UI_NAV_REFRESH_END <= UI_NAV_TOP, "aligned refresh must stop above navigation and marker");
_Static_assert(UI_NAV_MARKER_SCAN_END <= UI_NAV_TOP + 27, "marker crop must stop before navigation icons");
_Static_assert((UI_NAV_MARKER_TOP & 1) && !(UI_NAV_MARKER_BOTTOM & 1), "marker nibble guards match current geometry");

static bool main_nav_geometry(void) {
    // 当前板为倒置竖屏：逻辑 y 对应物理 x。其他屏型保持原刷新路径。
    // On this inverted-portrait board logical y maps to physical x; other layouts keep the existing presenter.
    return epd_get_rotation() == EPD_ROT_INVERTED_PORTRAIT &&
           epd_width() == 1216 && epd_height() == 684;
}

static EpdRect main_body_area(void) {
    // 内容截止到 y=1087，32列对齐也不能扩展到 y=1097 的小横条。
    // Content ends at y=1087, so 32-column expansion cannot reach the marker at y=1097.
    return (EpdRect){0, 0, epd_rotated_display_width(), UI_NAV_REFRESH_END};
}

static uint8_t main_bw_byte(uint8_t value) {
    return (value & 0x0f) >= 8 ? ((value & 0xf0) >= 0x80 ? 0xff : 0x0f)
                             : ((value & 0xf0) >= 0x80 ? 0xf0 : 0x00);
}

static bool main_gray_column(int x) {
    for (unsigned i = 0; i < s_main_band_count; ++i)
        if (x >= s_main_bands[i].y && x < s_main_bands[i].y + s_main_bands[i].height) return true;
    return false;
}

static void main_normalize_body(EpdiyHighlevelState* hl) {
    const size_t stride = (size_t)epd_width() / 2;
    for (int y = 0; y < epd_height(); ++y) {
        uint8_t* row = hl->front_fb + y * stride;
        if (!s_main_band_count) {
            for (int x = 0; x < UI_NAV_REFRESH_END / 2; x += 4) {
                uint32_t value;
                memcpy(&value, row + x, sizeof(value));
                const uint32_t binary = ((value & UINT32_C(0x88888888)) >> 3) * 15u;
                if (value != binary) memcpy(row + x, &binary, sizeof(binary));
            }
            continue;
        }
        for (int x = 0; x < UI_NAV_REFRESH_END / 2; ++x)
            if (!main_gray_column(x * 2)) row[x] = main_bw_byte(row[x]);
    }
}

static bool main_body_changed(EpdiyHighlevelState* hl) {
    const size_t stride = (size_t)epd_width() / 2;
    for (int y = 0; y < epd_height(); ++y)
        if (memcmp(hl->front_fb + y * stride, hl->back_fb + y * stride, UI_NAV_REFRESH_END / 2)) return true;
    return false;
}

// 三条最终灰阶最多约90KiB；首帧跳过它们，实际输出成功才提交back。
// Three final gray bands cost at most about 90 KiB; skip them in the first pass and commit back only after successful output.
static size_t main_band_bytes(void) {
    size_t bytes = 0;
    for (unsigned i = 0; i < s_main_band_count; ++i) bytes += (size_t)s_main_bands[i].height * epd_height() / 2;
    return bytes;
}
static void main_band_copy(EpdiyHighlevelState *hl, uint8_t *cache, bool restore) {
    const size_t stride = (size_t)epd_width() / 2;
    for (unsigned i = 0; i < s_main_band_count; ++i) {
        const size_t bytes = (size_t)s_main_bands[i].height / 2;
        for (int y = 0; y < epd_height(); ++y) {
            uint8_t *row = hl->front_fb + y * stride + s_main_bands[i].y / 2;
            if (restore) memcpy(row, cache, bytes);
            else { memcpy(cache, row, bytes); memcpy(row, hl->back_fb + y * stride + s_main_bands[i].y / 2, bytes); }
            cache += bytes;
        }
    }
}

static void main_freeze_navigation(EpdiyHighlevelState* hl, bool update_marker) {
    const size_t stride = (size_t)epd_width() / 2;
    for (int y = 0; y < epd_height(); ++y) {
        uint8_t* front = hl->front_fb + y * stride;
        const uint8_t* back = hl->back_fb + y * stride;
        if (!update_marker) {
            memcpy(front + UI_NAV_REFRESH_END / 2, back + UI_NAV_REFRESH_END / 2,
                   stride - UI_NAV_REFRESH_END / 2);
            continue;
        }
        // 只保留目标横条的五行；起点为奇数像素，同行字节的分隔线半字节仍取真实 back。
        // Keep only the five target marker rows; at the odd first pixel retain the neighboring divider nibble from actual back.
        memcpy(front + UI_NAV_REFRESH_END / 2, back + UI_NAV_REFRESH_END / 2,
               (UI_NAV_MARKER_TOP - UI_NAV_REFRESH_END) / 2);
        const int first = UI_NAV_MARKER_TOP / 2;
        front[first] = (front[first] & 0xf0) | (back[first] & 0x0f);
        memcpy(front + UI_NAV_MARKER_BOTTOM / 2, back + UI_NAV_MARKER_BOTTOM / 2,
               stride - UI_NAV_MARKER_BOTTOM / 2);
    }
}

void display_main_transition_disarm(void) {
    s_main_armed = NULL;
    s_main_shelf_page = false;
    s_main_band_count = 0;
    s_main_shelf_exit = false;
    s_main_changing_page = false;
}

void display_main_transition_cancel(void) {
    display_main_transition_disarm();
}

void display_main_transition_arm(const void* owner, bool leaving_shelf, bool changing_page) {
    s_main_armed = owner;
    s_main_shelf_page = false;
    s_main_band_count = 0;
    s_main_shelf_exit = s_main_armed && leaving_shelf;
    s_main_changing_page = s_main_armed && changing_page;
}

void display_main_transition_gray_bands(const EpdRect *bands, unsigned count) {
    if (!s_main_armed) return;
    s_main_band_count = 0;
    if (!bands || count > 3) return;
    int last = 0;
    for (unsigned i = 0; i < count; ++i) {
        EpdRect b = bands[i];
        if (b.x || b.width != epd_rotated_display_width() || b.y < last || b.height <= 0 ||
            (b.y & 1) || (b.height & 1) || b.y > UI_NAV_REFRESH_END || b.height > UI_NAV_REFRESH_END - b.y) return;
        s_main_bands[i] = b; last = b.y + b.height;
    }
    s_main_band_count = count;
}

void display_main_transition_shelf_page(void) {
    s_main_shelf_page = s_main_armed != NULL;
}

static enum EpdDrawError main_recover(EpdiyHighlevelState* hl, enum EpdDrawError error) {
    // 扫描失败后光学状态未知，必须从白底重建，不能复用半途图的 back。
    // A failed scan leaves unknown optical state; rebuild from white instead of reusing an intermediate back.
    display_main_transition_cancel();
    if (error & EPD_DRAW_EMPTY_LINE_QUEUE) {
        s_pclk_mhz = DISPLAY_PCLK_SAFE_MHZ;
        read_pico_epd_set_pclk(DISPLAY_PCLK_SAFE_MHZ);
    }
    use_scan_for(&E0470_WAVEFORM, MODE_GC16);
    epd_poweron();
    epd_clear();
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    enum EpdDrawError result = epd_hl_update_screen_from_white(hl, MODE_GC16, 25);
    s_main_fault = result != EPD_DRAW_SUCCESS;
    s_page_refreshes = 0;
    rails_keepalive();
    ESP_LOGW(TAG, "main transition recovery error=%d result=%d", error, result);
    return result;
}

static bool main_begin(EpdiyHighlevelState* hl, enum EpdDrawError* result) {
    const void* owner = s_main_armed;
    const bool shelf_exit = s_main_shelf_exit || s_main_shelf_page;
    s_main_shelf_page = false;
    const bool changing_page = s_main_changing_page;
    s_main_changing_page = false;
    const int refresh_mode = app_settings_main_refresh_mode();
    const bool water = refresh_mode == APP_MAIN_REFRESH_WATER;
    const bool normal = refresh_mode == APP_MAIN_REFRESH_NORMAL;
    const bool fast = refresh_mode == APP_MAIN_REFRESH_FAST;
    s_main_shelf_exit = false;
    s_main_armed = NULL;
    if (!owner || !hl || !hl->front_fb || !hl->back_fb) return false;
    if (s_main_fault) { *result = main_recover(hl, EPD_DRAW_SUCCESS); return true; }
    if (!main_nav_geometry()) return false;
    // 8帧整页方案被实机否定；用户接受主页面黑白、亚克力灰阶，黑白改用完整20相。
    // Hardware rejected whole-page 8-frame output; the user accepted black/white main pages with gray acrylic. Use the full 20-phase black/white table.
    main_freeze_navigation(hl, true);
    if (fast) main_normalize_body(hl);
    const bool changed = main_body_changed(hl);
    uint8_t *cache = fast && changed && s_main_band_count
        ? heap_caps_aligned_alloc(16, main_band_bytes(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    const bool gray_fallback = water || (fast && changed && s_main_band_count && !cache);
    // 黑白表的等灰迁移均不驱动；仅把有变化的横条并入同次扫描，固定图标保持原样。
    // The BW/exit tables hold every equal pair. Include only the changing marker
    // in this scan; frozen divider/labels/icons receive no drive. This removes
    // an additional comparison and eight-frame serial marker update.
    const bool marker_with_body=fast&&changed&&!cache&&!gray_fallback;
    EpdRect body_area=main_body_area();
    if(marker_with_body)body_area.height=UI_NAV_MARKER_SCAN_END;
    if (cache) main_band_copy(hl, cache, false);
    const EpdWaveform* body_wave = normal ? display_main_normal_waveform() : gray_fallback ? display_main_gray_waveform()
        : shelf_exit ? display_main_shelf_exit_waveform() : display_main_bw_waveform();
    enum EpdDrawMode body_mode = normal || gray_fallback ? MODE_GL16 : MODE_DU;
    bool body_full = false;
    // 普通切页只驱动变化像素，完整中灰相位不缩短；每十二次仍用原表清理内容区。
    // Ordinary changes drive differing pixels without shortening midgray phases; the original table still cleans the body every twelfth change.
    if (normal && changed) {
        ++s_page_refreshes;
        if (APP_UI_FAST_GC16_EVERY > 0 && s_page_refreshes >= APP_UI_FAST_GC16_EVERY) {
            s_page_refreshes = 0;
            body_wave = &E0470_WAVEFORM;
            body_mode = MODE_GC16;
            body_full = true;
        }
    }
    use_scan_for(body_wave, body_mode);
    epd_poweron();
    epd_hl_waveform(hl, body_wave);
    int64_t start = esp_timer_get_time();
    // 快刷/水波纹不攒强刷次数；普通的周期整理也不能进入底栏。
    // Fast/water accrue no forced-cleanup debt; ordinary periodic settling also excludes navigation.
    if(normal&&changing_page&&changed&&s_main_band_count&&!body_full){
        // 题头、封面和亚克力合并为同一轮校准GL16，不再按上下区域顺序输出。
        // Present header, covers and acrylic in one calibrated GL16 scan, without serial upper/lower updates.
        epd_hl_waveform(hl,&E0470_WAVEFORM);use_scan_for(&E0470_WAVEFORM,MODE_GL16);
        *result=epd_hl_update_area_full(hl,MODE_GL16,25,body_area);
    } else if (water && changing_page && changed) {
        // 水波纹直接输出最终灰阶图，不先刷黑白、不追加整页灰阶；失败由统一恢复处理。
        // Water outputs the final gray target directly, without a BW prepass or a second whole-page gray update; common recovery handles failure.
        read_pico_epd_use_scan(READ_PICO_EPD_SCAN_FAST);
        epd_lcd_set_prefill_lines(127);
        EpdRect ripple = main_body_area();
        const int prior_tick=e0470_page_turn_tick_us();
        e0470_page_turn_set_tick_us(E0470_TURN_FAST_TICK_US);
        *result = e0470_page_turn_with_waveform_compact(hl, ripple, E0470_TURN_RTL,
                                                       display_main_water_waveform(), E0470_TURN_FAST_BANDS);
        e0470_page_turn_set_tick_us(prior_tick);
        if (*result == EPD_DRAW_NO_PHASES_AVAILABLE) {
            use_scan_for(body_wave, MODE_GL16);
            epd_hl_waveform(hl, body_wave);
            *result = epd_hl_update_area(hl, MODE_GL16, 25, ripple);
        }
        // 主页面动画不长期保留相位缓存，避免与联网和阅读资源争用。
        // Release main-animation LUTs instead of competing with networking and reader resources.
        e0470_page_turn_release();
    } else if (!changed) *result = EPD_DRAW_SUCCESS;
    else *result = body_full ? epd_hl_update_area_full(hl, body_mode, 25, body_area)
                            : epd_hl_update_area(hl, body_mode, 25, body_area);
    if (cache) {
        main_band_copy(hl, cache, true);
        heap_caps_free(cache);
        if (*result == EPD_DRAW_SUCCESS) {
            const EpdRect last = s_main_bands[s_main_band_count - 1];
            EpdRect gray = {0, s_main_bands[0].y, epd_rotated_display_width(), last.y + last.height - s_main_bands[0].y};
            // 采用原校准迁移，不自行按灰度比例截帧；等灰无需反复推动。
            // Retain calibrated transitions without scaling pulses by gray distance; equal grays need no repeated drive.
            epd_hl_waveform(hl, display_main_gray_waveform());
            use_scan_for(display_main_gray_waveform(), MODE_GL16);
            *result = epd_hl_update_area(hl, MODE_GL16, 25, gray);
        }
    }
    s_main_band_count = 0;
    if (*result == EPD_DRAW_SUCCESS && !marker_with_body) {
        const EpdRect marker = {0, UI_NAV_MARKER_TOP, epd_rotated_display_width(),
                               UI_NAV_MARKER_BOTTOM - UI_NAV_MARKER_TOP};
        // 同页重绘横条无变化时 highlevel 自动跳过；32列扩展范围中其余像素来自 back，保持不驱动。
        // Highlevel skips an unchanged marker; all other pixels in its expanded 32-column crop come from back and remain undriven.
        epd_hl_waveform(hl, &E0470_FOLLOW_WAVEFORM);
        use_scan_for(&E0470_FOLLOW_WAVEFORM, MODE_DU);
        *result = epd_hl_update_area(hl, MODE_DU, 25, marker);
    }
    if (*result != EPD_DRAW_SUCCESS) *result = main_recover(hl, *result);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    use_scan_for(&E0470_WAVEFORM, MODE_GL16);
    rails_keepalive();
    ESP_LOGI(TAG, "main refresh mode=%d shelf_exit=%d draw=%lldms result=%d", app_settings_main_refresh_mode(), shelf_exit, (esp_timer_get_time() - start) / 1000, *result);
    (void)start;
    return true;
}

// 其他显示出口只取消未用入口；主页面失败时先重建完整目标，绝不回放旧缓存。
// Other outputs disarm unused entry permission; a main-page failure first rebuilds the full current target, never replaying old cache.
static bool main_before_update(EpdiyHighlevelState* hl, enum EpdDrawError* result) {
    display_main_transition_cancel();
    if (s_main_fault) { *result = main_recover(hl, EPD_DRAW_SUCCESS); return true; }
    return false;
}

// 所有按 fb 刷屏的出口都经这里。GL16 必须全像素（白底补 1 帧靠它打到）。
// 整页差分刷攒够 APP_UI_GC16_EVERY 次就把这一次升为全像素 GC16：区域、fb 都不变，只换模式，
// 屏上内容仍由 fb 决定，不会丢；全像素是为了让未变化像素也过一遍 LUT，否则压不掉灰底。
// 跟随 DU 波形只有 DU 一张表，不计数也不升级。
// Every fb present goes through here. GL16 must be full-pixel (the extra white
// frame depends on that). After APP_UI_GC16_EVERY whole-page updates, this one is
// promoted to full-pixel GC16: area and fb stay the same, only the mode
// changes, so content is not lost. Full-pixel is so unchanged pixels also run
// the LUT; otherwise the gray floor will not clear. FOLLOW DU has only a DU
// table and does not count or promote.
static enum EpdDrawError hl_update(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode, bool full,
    const EpdRect* area, bool allow_gl16_diff
) {
    // 已显示主页面的局部封面或滚动更新同样向内裁剪，防止32列对齐带出小横条。
    // Local cover or scroll updates on an already displayed main page use the same inward crop, excluding the marker after alignment.
    if (area && s_main_shelf_page && app_settings_main_fast_refresh()) {
        enum EpdDrawError result;
        if (main_begin(hl, &result)) return result;
    }
    EpdRect clipped;
    const bool protected_area = area && s_main_armed && !s_main_fault && main_nav_geometry();
    if (protected_area) {
        main_freeze_navigation(hl, false);
        if (app_settings_main_fast_refresh()) main_normalize_body(hl);
        clipped = *area;
        if ((int64_t)clipped.y + clipped.height > UI_NAV_REFRESH_END)
            clipped.height = UI_NAV_REFRESH_END - clipped.y;
        if (clipped.height <= 0) { display_main_transition_disarm(); return EPD_DRAW_SUCCESS; }
        area = &clipped;
        // 显式短局刷不被主页模式改写，猫眼/秒针在普通与水波纹中采用同一动效。
        // Preserve explicit short partial updates so cat eyes and seconds share ordinary-mode motion in ripple mode.
        if (!full && waveform != &E0470_FOLLOW_WAVEFORM && (mode & 0x0f) != MODE_GC16 &&
            (app_settings_main_refresh_mode()!=APP_MAIN_REFRESH_NORMAL || (mode & 0x0f)==MODE_GL16)) {
            bool has_gray = !app_settings_main_fast_refresh();
            for (unsigned i = 0; i < s_main_band_count; ++i)
                has_gray |= clipped.y < s_main_bands[i].y + s_main_bands[i].height && clipped.y + clipped.height > s_main_bands[i].y;
            waveform = app_settings_main_refresh_mode()==APP_MAIN_REFRESH_NORMAL ? display_main_normal_waveform() :
                has_gray ? display_main_gray_waveform() : s_main_shelf_page
                ? display_main_shelf_exit_waveform() : display_main_bw_waveform();
            mode = has_gray ? MODE_GL16 : MODE_DU;
            allow_gl16_diff = true;
            epd_hl_waveform(hl, waveform);
        }
    }
    enum EpdDrawError intercepted;
    if (main_before_update(hl, &intercepted)) return intercepted;
    if (waveform != &E0470_FOLLOW_WAVEFORM && area == NULL) {
        if ((mode & 0xF) == MODE_GC16) {
            s_page_refreshes = 0;
        } else if (APP_UI_GC16_EVERY > 0 && ++s_page_refreshes >= APP_UI_GC16_EVERY) {
            s_page_refreshes = 0;
            mode = (enum EpdDrawMode)((mode & ~0xF) | MODE_GC16);
            full = true;
            ESP_LOGI(TAG, "promote to GC16 after %d page transitions", APP_UI_GC16_EVERY);
        }
    }
    full = full || ((mode & 0xF) == MODE_GL16 && !allow_gl16_diff);
    // 比较完成后只切换一次扫描档，避免快刷前先切 FULL 再切 FAST。
    // Select scan timing once after comparison, without a FULL-to-FAST round trip before fast updates.
    use_scan_for(waveform, mode);
    enum EpdDrawError result;
    if (area != NULL) {
        result = full ? epd_hl_update_area_full(hl, mode, 25, *area)
                    : epd_hl_update_area(hl, mode, 25, *area);
    } else result = full ? epd_hl_update_screen_full(hl, mode, 25) : epd_hl_update_screen(hl, mode, 25);

    return protected_area && result != EPD_DRAW_SUCCESS ? main_recover(hl, result) : result;
}

// gen19（真机 Bug4）：推送会话活跃（手机连着收数）时，BLE notify 突发与整屏
// 扫描争用 PSRAM/总线带宽，18 MHz 供数断流——epdiy line buffer underrun →
// 回落 12 MHz 重扫，一次刷新付两遍钱，且真机实证欠载后粘滞安全档拖慢后续
// 全部翻页。策略：每次刷新入口按会话状态选档——会话活跃直接走安全档（省掉
// 「先失败再回落」），会话结束恢复默认档。欠载兜底的粘滞语义保留：真欠载过
// （s_pclk_sticky）的设备维持安全档直到重启，与 gen10 裁定一致。
// gen19 (real-hw Bug4): while a push session is live, BLE notify bursts
// contend with the scan for PSRAM/bus bandwidth and the 18 MHz line supply
// starves - the epdiy underrun costs a failed attempt plus a 12 MHz rescan
// (double latency) and, as real-hw proved, leaves the clock sticky-slow for
// every later page turn. Policy: pick the clock by session state at each
// refresh entry - active sessions go straight to the safe clock (no failed
// attempt), idle restores the default. The underrun fallback keeps its sticky
// semantics: a device that truly starved (s_pclk_sticky) stays safe until
// reboot, per the gen10 ruling.
static bool s_pclk_sticky;
static void pclk_policy_refresh(void) {
    const push_state_t st = push_sink_state();
    const bool active = (st == PUSH_STATE_CONNECTED || st == PUSH_STATE_RECEIVING || st == PUSH_STATE_PROCESSING);
    const int want = (active || s_pclk_sticky) ? DISPLAY_PCLK_SAFE_MHZ : DISPLAY_PCLK_DEFAULT_MHZ;
    if (s_pclk_mhz == want) return;
    s_pclk_mhz = want;
    read_pico_epd_set_pclk(want);
    ESP_LOGI(TAG, "pclk -> %d MHz (push session %s)", want, active ? "active" : "idle");
}

static enum EpdDrawError update_display_mode_unlocked(
    EpdiyHighlevelState* hl, enum EpdDrawMode mode
) {
    pclk_policy_refresh();
    epd_poweron();
    enum EpdDrawError result = hl_update(hl, &E0470_WAVEFORM, mode, false, NULL, false);
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_mode_diff_unlocked(
    EpdiyHighlevelState* hl, enum EpdDrawMode mode
) {
    pclk_policy_refresh();
    epd_poweron();
    enum EpdDrawError result = hl_update(hl, &E0470_WAVEFORM, mode, false, NULL, true);
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_fast_page_unlocked(EpdiyHighlevelState* hl) {
    pclk_policy_refresh();
    enum EpdDrawError navigation;
    if (main_begin(hl, &navigation)) return navigation;
    enum EpdDrawError intercepted;
    if (main_before_update(hl, &intercepted)) return intercepted;
    if (APP_UI_FAST_GC16_EVERY > 0 && ++s_page_refreshes >= APP_UI_FAST_GC16_EVERY) {
        s_page_refreshes = 0;
        use_scan_for(&E0470_WAVEFORM, MODE_GC16);
        epd_poweron();
        epd_hl_waveform(hl, &E0470_WAVEFORM);
        enum EpdDrawError result = epd_hl_update_screen_full(hl, MODE_GC16, 25);

        rails_keepalive();
        ESP_LOGI(TAG, "fast navigation cleanup after %d transitions", APP_UI_FAST_GC16_EVERY);
        return result;
    }
    const bool ordinary = app_settings_main_refresh_mode() == APP_MAIN_REFRESH_NORMAL;
    const EpdWaveform* waveform = ordinary ? display_main_normal_waveform() : &E0470_WAVEFORM;
    use_scan_for(waveform, MODE_GL16);
    epd_poweron();
    epd_hl_waveform(hl, waveform);
    // 普通子页面与四主页使用同一柔和灰阶，不另加黑白预刷或延迟补刷；其他模式保留原周期。
    // Ordinary subpages share the roots' soft grayscale in one pass, without BW prepasses or delayed output; other modes retain their cadence.
    const bool settle = !ordinary && APP_UI_FAST_GL16_SETTLE_EVERY > 0 &&
                        s_page_refreshes % APP_UI_FAST_GL16_SETTLE_EVERY == 0;
    enum EpdDrawError result = settle ? epd_hl_update_screen_full(hl, MODE_GL16, 25)
                                      : epd_hl_update_screen(hl, MODE_GL16, 25);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_from_white_with_unlocked(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
) {
    pclk_policy_refresh();
    enum EpdDrawError intercepted;
    if (main_before_update(hl, &intercepted)) return intercepted;
    use_scan_for(waveform, mode);
    epd_poweron();
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = epd_hl_update_screen_from_white(hl, mode, 25);

    epd_hl_waveform(hl, &E0470_WAVEFORM);
    s_page_refreshes = 0;
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_from_white_unlocked(EpdiyHighlevelState* hl) {
    return update_display_from_white_with(hl, &E0470_WAVEFORM, MODE_GC16);
}

static enum EpdDrawError update_display_white_unlocked(EpdiyHighlevelState* hl) {
    epd_hl_set_all_white(hl);
    return update_display_full(hl);
}

static bool s_white_exit;

void display_hold_white_exit(bool hold) {
    s_white_exit = hold;
}

bool display_take_white_exit(void) {
    const bool hold = s_white_exit;
    s_white_exit = false;
    return hold;
}

static enum EpdDrawError update_display_full_unlocked(EpdiyHighlevelState* hl) {
    pclk_policy_refresh();
    s_full_refresh_requested = false;
    display_main_transition_cancel();
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    epd_poweron();
    enum EpdDrawError result = hl_update(hl, &E0470_WAVEFORM, MODE_GC16, true, NULL, false);
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_area_diff_with_unlocked(EpdiyHighlevelState*, const EpdWaveform*, enum EpdDrawMode, EpdRect);
static enum EpdDrawError update_display_area_full_with_unlocked(EpdiyHighlevelState*, const EpdWaveform*, enum EpdDrawMode, EpdRect);

static enum EpdDrawError update_display_image_gray_unlocked(EpdiyHighlevelState* hl) {
    enum EpdDrawError intercepted;
    if (main_before_update(hl, &intercepted)) return intercepted;
    // 图片保留十六级原始灰阶；白底和完整 48 相波形稳定呈现层次。
    // Images keep sixteen source grays; a white baseline and the full 48-phase table preserve tones.
    use_scan_for(&E0470_FULL_WAVEFORM, MODE_GC16);
    epd_poweron();
    epd_clear();
    epd_hl_waveform(hl, &E0470_FULL_WAVEFORM);
    enum EpdDrawError result = epd_hl_update_screen_from_white(hl, MODE_GC16, 25);

    epd_hl_waveform(hl, &E0470_WAVEFORM);
    s_page_refreshes = 0;
    rails_keepalive();
    return result;
}

// 指定波形整屏刷一次，刷完把默认波形装回去。用来 A/B 两条灰阶表。
// Present the whole screen with a given waveform, then restore the default. Used to A/B two gray tables.
static enum EpdDrawError update_display_with_unlocked(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
) {
    pclk_policy_refresh();
    epd_poweron();
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = hl_update(hl, waveform, mode, false, NULL, false);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_area_with_unlocked(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    pclk_policy_refresh();
    epd_poweron();
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = hl_update(hl, waveform, mode, false, &area, false);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_area_diff_with_unlocked(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    pclk_policy_refresh();
    epd_poweron();
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = hl_update(hl, waveform, mode, false, &area, true);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_area_full_with_unlocked(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    pclk_policy_refresh();
    epd_poweron();
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = hl_update(hl, waveform, mode, true, &area, false);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_water_turn_unlocked(EpdiyHighlevelState* hl, EpdRect area,
                                             e0470_turn_dir_t dir) {
    enum EpdDrawError intercepted;
    if (main_before_update(hl, &intercepted)) return intercepted;
    // 阅读页显式选择时调用；主页面水波纹有独立裁剪路径，普通模式不受影响。
    // Explicit reader effect; main-tab water has its own bounded path, and ordinary mode is unaffected.
    read_pico_epd_use_scan(READ_PICO_EPD_SCAN_FAST);
    epd_lcd_set_prefill_lines(s_bulk_io ? 127 : 64);
    epd_poweron();
    enum EpdDrawError result = e0470_page_turn(hl, area, dir);
    use_scan_for(&E0470_WAVEFORM, MODE_GL16);
    rails_keepalive();
    if (result == EPD_DRAW_SUCCESS) return result;
    if (result == EPD_DRAW_NO_PHASES_AVAILABLE) {
        ESP_LOGW(TAG, "water turn phases unavailable; use regular GL16");
        return update_display_area_with(hl, &E0470_WAVEFORM, MODE_GL16, area);
    }
    // 中断可能把物理屏留在半途。清屏后按目标 front 重建整屏基准。
    // An interrupted scan may leave intermediate pixels. Rebuild the whole baseline from white.
    if (result & EPD_DRAW_EMPTY_LINE_QUEUE) {
        s_pclk_mhz = DISPLAY_PCLK_SAFE_MHZ;
        s_pclk_sticky = true; // gen19：真欠载 → 安全档粘滞到重启（gen10 语义）
        read_pico_epd_set_pclk(DISPLAY_PCLK_SAFE_MHZ);
    }
    use_scan_for(&E0470_WAVEFORM, MODE_GC16);
    epd_clear();
    enum EpdDrawError recovered = epd_hl_update_screen_from_white(hl, MODE_GC16, 25);

    s_page_refreshes = 0;
    rails_keepalive();
    ESP_LOGW(TAG, "water turn failed (%d), recovery=%d", result, recovered);
    return recovered;
}

// 供数不足时的兜底：把频率退回安全值，整屏白一次，让后面的差分刷有干净参考帧。
// Underrun fallback: drop to the safe clock and wipe the panel white so later differentials have a clean reference.
int display_pclk_mhz(void) { return s_pclk_mhz; }

static void guard_draw_result_unlocked(EpdiyHighlevelState* hl, enum EpdDrawError result) {
    if (!(result & EPD_DRAW_EMPTY_LINE_QUEUE)) return;
    display_main_transition_cancel();
    s_pclk_mhz = DISPLAY_PCLK_SAFE_MHZ;
    s_pclk_sticky = true; // gen19：真欠载 → 安全档粘滞到重启（gen10 语义）
    read_pico_epd_set_pclk(DISPLAY_PCLK_SAFE_MHZ);
    use_scan_for(&E0470_WAVEFORM, MODE_GC16);
    epd_poweron();
    epd_clear();
    // 清物理屏后仅重置旧帧基准，保留目标页；否则局部刷新会留下整页白屏。
    // Reset only the old-frame baseline after clearing; preserving the target prevents blank pages after partial updates.
    enum EpdDrawError recovered = epd_hl_update_screen_from_white(hl, MODE_GC16, 25);

    s_main_fault = recovered != EPD_DRAW_SUCCESS;

    if (recovered != EPD_DRAW_SUCCESS) ESP_LOGW(TAG, "underrun recovery failed (%d)", recovered);
    s_page_refreshes = 0;
    rails_keepalive();
    ESP_LOGW(TAG, "line queue underrun, pclk back to %d MHz, recovery=%d", DISPLAY_PCLK_SAFE_MHZ, recovered);
}

#ifdef KIIKO_LOCAL_LATENCY
int64_t local_display_elapsed_us;
#define LATENCY_DISPLAY_BEGIN() const int64_t local_display_start=esp_timer_get_time()
#define LATENCY_DISPLAY_END() (local_display_elapsed_us+=esp_timer_get_time()-local_display_start)
#else
#define LATENCY_DISPLAY_BEGIN() do {} while(0)
#define LATENCY_DISPLAY_END() do {} while(0)
#endif

// 连续相位全部完成后，back已是真实黑白画面；不因尚未补灰阶而多做一次整屏GC16。
// Once continuous phases finish, back is the real binary frame; pending gray restoration alone does not require full GC16.
static bool display_needs_full(void) { return s_full_refresh_requested; }

// 统一硬件出口，递归调用也必须覆盖整个扫描过程。/ Guard complete scans, including recursive fallback paths.
enum EpdDrawError update_display_mode(
    EpdiyHighlevelState* hl, enum EpdDrawMode mode
) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_mode_unlocked(hl, mode);
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_mode_diff(
    EpdiyHighlevelState* hl, enum EpdDrawMode mode
) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_mode_diff_unlocked(hl, mode);
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_fast_page(EpdiyHighlevelState* hl) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_fast_page_unlocked(hl);
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_from_white_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_from_white_with_unlocked(hl, waveform, mode);
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_from_white(EpdiyHighlevelState* hl) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_from_white_unlocked(hl);
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_white(EpdiyHighlevelState* hl) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    s_full_refresh_requested = false;
    enum EpdDrawError result = update_display_white_unlocked(hl);
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_full(EpdiyHighlevelState* hl) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = update_display_full_unlocked(hl);
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

// 屏幕修复的遍数与单帧时长：遍数是盲刷黑/白走几轮，越多越彻底、整段也越久。
// Panel repair passes and per-frame time: the count is how many black/white rounds the blind drive
// walks -- more is more thorough and takes longer.
#define DISPLAY_REPAIR_CYCLES 6
#define DISPLAY_REPAIR_TIME 15
// 掉电保持时长：够面板把停在半帧的像素放掉，又不至于让用户等。
// How long the rails stay down: enough for pixels stuck mid-frame to let go, short enough to wait.
#define DISPLAY_REPAIR_OFF_MS 400

// 面板修复：整屏盲驱动，不看 framebuffer。刷新中途断电会让面板停在半帧状态，而这里所有常规
// 刷新都是差分的（只重写与 RAM 上一帧不同的像素与相位），停在半路的那些行永远轮不到重写——
// 现象就是怎么刷新都留着一道条纹。这条路把每一行按同一电平重新驱动几轮，最后停在白色。
// Panel repair: a blind full-screen drive that ignores the framebuffer. A power cut in the middle of
// a refresh leaves the panel stuck mid-frame, and every ordinary path here is differential -- it
// rewrites only what differs from the RAM frame -- so those rows are never driven again and the
// stripe survives every refresh. This drives every row to the same level for several rounds and ends
// on white.
enum EpdDrawError display_panel_repair(EpdiyHighlevelState* hl) {
    if (!hl || !hl->front_fb || !hl->back_fb) return EPD_DRAW_INVALID_CROP;
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    continuous_du_reset();
    s_full_refresh_requested = false;
    display_main_transition_cancel();
    // 先真掉一次电：VCOM/VGH/VGL 重新建立，停在半路的像素失去保持电压。
    // One real power cycle first: VCOM/VGH/VGL are rebuilt and pixels stuck mid-frame lose their hold.
    epd_poweroff();
    vTaskDelay(pdMS_TO_TICKS(DISPLAY_REPAIR_OFF_MS));
    use_scan_for(&E0470_WAVEFORM, MODE_GC16);
    epd_poweron();
    // 盲刷黑/白：与 framebuffer 无关；每轮之间让出 CPU，整段要几秒。
    // Blind black/white drive, independent of the framebuffer; the CPU is released between rounds
    // because the whole pass takes seconds.
    const EpdRect area = epd_full_screen();
    for (int cycle = 0; cycle < DISPLAY_REPAIR_CYCLES; ++cycle) {
        for (int i = 0; i < 10; ++i) epd_push_pixels(area, DISPLAY_REPAIR_TIME, 0);
        for (int i = 0; i < 13; ++i) epd_push_pixels(area, DISPLAY_REPAIR_TIME, 1);
        for (int i = 0; i < 3; ++i) epd_push_pixels(area, DISPLAY_REPAIR_TIME, 2);
        vTaskDelay(1);
    }
    // 面板刚被盲刷成白：把「面板显示的是白」这件事也写进基准，然后整屏推一次当前画面，连底栏
    // 一起推回去。这里不能用 epd_hl_set_all_white()——它清的是绘制缓冲（页面画在 front_fb 上），
    // 会把用户正看的那一页擦掉；修复只该动基准，画面由这一记整屏刷新重新落到面板上。
    // The blind drive left the panel white: the baseline is told so, and one full update pushes the
    // current frame -- navigation bar included -- back onto it. epd_hl_set_all_white() must not be
    // used here: it clears the *drawing* buffer (pages draw into front_fb) and would wipe the page
    // the user is looking at. The repair touches the baseline only; this full update re-lands the
    // frame on the panel.
    // 直接走受锁保护的整屏白底出口，包含底栏；不能被主页裁剪拦截，也不再重复推页。
    // Use the locked full-frame white-baseline path including navigation, without a main-page crop or duplicate page push.
    enum EpdDrawError result = update_display_from_white_with_unlocked(hl, &E0470_WAVEFORM, MODE_GC16);
    if (result != EPD_DRAW_SUCCESS) result = main_recover(hl, result);
    s_main_fault = result != EPD_DRAW_SUCCESS;
    if (s_main_fault) s_full_refresh_requested = true;
    rails_keepalive();
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_image_gray(EpdiyHighlevelState* hl) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    s_full_refresh_requested = false;
    enum EpdDrawError result = update_display_image_gray_unlocked(hl);
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_with_unlocked(hl, waveform, mode);
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_area_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_area_with_unlocked(hl, waveform, mode, area);
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_area_diff_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_area_diff_with_unlocked(hl, waveform, mode, area);
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_area_full_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_area_full_with_unlocked(hl, waveform, mode, area);
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_water_turn(EpdiyHighlevelState* hl, EpdRect area,
                                             e0470_turn_dir_t dir) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_water_turn_unlocked(hl, area, dir);
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

void guard_draw_result(EpdiyHighlevelState* hl, enum EpdDrawError result) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    guard_draw_result_unlocked(hl, result);
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
}
