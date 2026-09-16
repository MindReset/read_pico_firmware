/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 刷屏出口：按模式推屏、HV 轨空闲下电、pclk 欠载回退。
 *
 * Present path: push by mode, drop HV rails on idle, fall back pclk on
 * underrun.
 */

#include "display.h"

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "e0470_epaper_waveform.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "read_pico";

// HV 轨道空闲多久才断电。断电要等 500ms 放电，再上电又要几十毫秒，
// 所以连续操作期间一直保持常开，只有真的没人动才关掉省电并卸掉 VCOM。
// How long HV rails stay up when idle. Power-off waits 500 ms to discharge,
// and power-on takes tens of ms, so keep them on during a burst of work and
// drop them — and VCOM — only when nothing is happening.
#define RAILS_IDLE_TIMEOUT_MS 8000

// 0 表示轨道已断电；否则是到期时间（ms），到点后主循环断电。
// 0 means the rails are off; otherwise a deadline (ms) after which the loop powers them down.
static int64_t rails_deadline_ms;

void rails_keepalive(void) {
    rails_deadline_ms = esp_timer_get_time() / 1000 + RAILS_IDLE_TIMEOUT_MS;
}

void rails_idle_check(int64_t now_ms) {
    if (rails_deadline_ms != 0 && now_ms >= rails_deadline_ms) {
        rails_deadline_ms = 0;
        epd_poweroff();
    }
}

// 20 相完整/原厂 DU 按厂家时序走 FULL；只有触摸笔迹用的 8 帧跟随 DU 走 FAST。
// 20-phase full / vendor DU uses FULL timing; the 8-frame FOLLOW DU used for ink trails uses FAST.
static void use_scan_for(const EpdWaveform* waveform, enum EpdDrawMode mode) {
    const bool fast = (mode & 0xF) == MODE_DU && waveform == &E0470_FOLLOW_WAVEFORM;
    read_pico_epd_use_scan(fast ? READ_PICO_EPD_SCAN_FAST : READ_PICO_EPD_SCAN_FULL);
}

// 自上次 GC16 以来的差分刷（DU/GL16）次数。
// Soft (DU/GL16) updates since the last GC16.
static int s_soft_refreshes;

// 所有按 fb 刷屏的出口都经这里。GL16 必须全像素（白底补 1 帧靠它打到）。
// 差分刷攒够 APP_GC16_EVERY 次就把这一次升为全像素 GC16：区域、fb 都不变，只换模式，
// 屏上内容仍由 fb 决定，不会丢；全像素是为了让未变化像素也过一遍 LUT，否则压不掉灰底。
// 跟随 DU 波形只有 DU 一张表，不计数也不升级。
// Every fb present goes through here. GL16 must be full-pixel (the extra white
// frame depends on that). After APP_GC16_EVERY soft updates, this one is
// promoted to full-pixel GC16: area and fb stay the same, only the mode
// changes, so content is not lost. Full-pixel is so unchanged pixels also run
// the LUT; otherwise the gray floor will not clear. FOLLOW DU has only a DU
// table and does not count or promote.
static enum EpdDrawError hl_update(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode, bool full,
    const EpdRect* area
) {
    full = full || (mode & 0xF) == MODE_GL16;
    if (waveform != &E0470_FOLLOW_WAVEFORM) {
        if ((mode & 0xF) == MODE_GC16) {
            s_soft_refreshes = 0;
        } else if (APP_GC16_EVERY > 0 && ++s_soft_refreshes >= APP_GC16_EVERY) {
            s_soft_refreshes = 0;
            mode = (enum EpdDrawMode)((mode & ~0xF) | MODE_GC16);
            full = true;
            ESP_LOGI(TAG, "promote to GC16 after %d soft refreshes", APP_GC16_EVERY);
        }
    }
    if (area != NULL) {
        return full ? epd_hl_update_area_full(hl, mode, 25, *area)
                    : epd_hl_update_area(hl, mode, 25, *area);
    }
    return full ? epd_hl_update_screen_full(hl, mode, 25) : epd_hl_update_screen(hl, mode, 25);
}

enum EpdDrawError update_display_mode(
    EpdiyHighlevelState* hl, enum EpdDrawMode mode
) {
    use_scan_for(&E0470_WAVEFORM, mode);
    epd_poweron();
    enum EpdDrawError result = hl_update(hl, &E0470_WAVEFORM, mode, false, NULL);
    rails_keepalive();
    return result;
}

enum EpdDrawError update_display_from_white_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
) {
    use_scan_for(waveform, mode);
    epd_poweron();
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = epd_hl_update_screen_from_white(hl, mode, 25);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    s_soft_refreshes = 0;
    rails_keepalive();
    return result;
}

enum EpdDrawError update_display_from_white(EpdiyHighlevelState* hl) {
    return update_display_from_white_with(hl, &E0470_WAVEFORM, MODE_GC16);
}

enum EpdDrawError update_display_white(EpdiyHighlevelState* hl) {
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

enum EpdDrawError update_display_full(EpdiyHighlevelState* hl) {
    use_scan_for(&E0470_WAVEFORM, MODE_GC16);
    epd_poweron();
    enum EpdDrawError result = hl_update(hl, &E0470_WAVEFORM, MODE_GC16, true, NULL);
    rails_keepalive();
    return result;
}

// 指定波形整屏刷一次，刷完把默认波形装回去。用来 A/B 两条灰阶表。
// Present the whole screen with a given waveform, then restore the default. Used to A/B two gray tables.
enum EpdDrawError update_display_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
) {
    use_scan_for(waveform, mode);
    epd_poweron();
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = hl_update(hl, waveform, mode, false, NULL);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    rails_keepalive();
    return result;
}

enum EpdDrawError update_display_area_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    use_scan_for(waveform, mode);
    epd_poweron();
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = hl_update(hl, waveform, mode, false, &area);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    rails_keepalive();
    return result;
}

// 供数不足时的兜底：把频率退回安全值，整屏白一次，让后面的差分刷有干净参考帧。
// Underrun fallback: drop to the safe clock and wipe the panel white so later differentials have a clean reference.
static int s_pclk_mhz = DISPLAY_PCLK_DEFAULT_MHZ;

int display_pclk_mhz(void) { return s_pclk_mhz; }

void guard_draw_result(EpdiyHighlevelState* hl, enum EpdDrawError result) {
    if (!(result & EPD_DRAW_EMPTY_LINE_QUEUE)) return;
    s_pclk_mhz = DISPLAY_PCLK_SAFE_MHZ;
    read_pico_epd_set_pclk(DISPLAY_PCLK_SAFE_MHZ);
    use_scan_for(&E0470_WAVEFORM, MODE_GC16);
    epd_poweron();
    epd_clear();
    epd_hl_set_all_white(hl);
    epd_hl_update_screen(hl, MODE_GC16, 25);
    s_soft_refreshes = 0;
    rails_keepalive();
    ESP_LOGW(TAG, "line queue underrun, pclk back to %d MHz", DISPLAY_PCLK_SAFE_MHZ);
}
