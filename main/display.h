/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 墨水屏刷新、HV 轨空闲超时、pclk 回退。
 *
 * EPD present, HV-rail idle timeout, and pclk fallback.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "epd_highlevel.h"
#include "epdiy.h"
#include "e0470_page_turn.h"
#include "read_pico_epd_timing.h"

#ifdef __cplusplus
extern "C" {
#endif

// LCD 像素时钟：只决定有效像素段占一行的多少，剩下的由行结束段补足——行周期被锁在
// 波形标定的帧周期上，所以调 pclk 不会让刷新变快，只影响 DMA 的供数余量。12MHz 是
// epdiy 的保守默认值，18MHz 实测稳定（前提是 PSRAM 跑在 120MHz，80MHz 下 16MHz
// 就喂不满 DMA），开机锁 18MHz 留足余量。
// LCD pixel clock: it only sets how much of the line the active pixels occupy;
// the line-end pad fills the rest. The line period is locked to the waveform
// frame time, so pclk does not make refresh faster — it only changes DMA slack.
// 12 MHz is epdiy's conservative default; 18 MHz is stable here when PSRAM runs
// at 120 MHz (at 80 MHz even 16 MHz starves DMA). Boot locks 18 MHz for margin.
#define DISPLAY_PCLK_DEFAULT_MHZ 18
// 出现供数不足（EPD_DRAW_EMPTY_LINE_QUEUE）时退回这个确定安全的频率。
// Fall back to this known-safe clock on underrun (EPD_DRAW_EMPTY_LINE_QUEUE).
#define DISPLAY_PCLK_SAFE_MHZ READ_PICO_EPD_PCLK_MIN_MHZ
// 工程页上还能继续往上试。行消隐与 CKV 宽度会跟着频率重解。
// The lab page can still step higher. Line blanking and CKV width re-solve with the clock.
#define DISPLAY_PCLK_MIN_MHZ READ_PICO_EPD_PCLK_MIN_MHZ
#define DISPLAY_PCLK_MAX_MHZ READ_PICO_EPD_PCLK_MAX_MHZ
#define DISPLAY_PCLK_STEP_MHZ 1

/// 保留高压轨并释放像素驱动，包括无变化刷新；不增加扫描。/ Retain HV rails and release pixel drive, including no-change updates, without another scan.
void rails_keepalive(void);
void rails_idle_check(int64_t now_ms);

/// 大量文件I/O期间增加扫描预填，调用方离开时恢复。/ Increase scan prefill during bulk file I/O; caller restores on exit.
void display_set_bulk_io(bool active);

/// 请求下一张完成绘制的画面整屏清理一次，用于解锁。/ Clean the next completed frame once after unlock.
void display_request_full_refresh(void);
enum EpdDrawError update_display_mode(EpdiyHighlevelState* hl, enum EpdDrawMode mode);
/// 普通页面切换使用真实差分 GL16；未变化像素不进入 dirty map。
/// True differential GL16 for ordinary page transitions; unchanged pixels stay clean.
enum EpdDrawError update_display_mode_diff(EpdiyHighlevelState* hl, enum EpdDrawMode mode);
/// 系统页切换：普通模式柔和16灰阶差分，等灰保持、黑白端点单向推动，周期GC16清残影；不安排二次刷新。
/// System transitions: ordinary soft 16-gray differences hold equal grays and drive BW endpoints directionally, with periodic GC16 cleanup and no second pass.
/// 四主页面固定底栏，仅横条局部更新；普通保留灰阶与内容区整理，快刷用完整黑白表及棋盘格亚克力。
/// All main screens retain navigation with local markers; ordinary uses soft gray differences and periodic body cleanup, fast uses monochrome/checkerboard acrylic.
enum EpdDrawError update_display_fast_page(EpdiyHighlevelState* hl);
/// 为下一次主页面输出启用固定底栏；标记书架切出与真正切页，避免同页重绘播放动画。
/// Arm fixed navigation for the next main-screen output; tag shelf exit and actual navigation so same-page repaints do not animate.
void display_main_transition_arm(const void* owner, bool leaving_shelf, bool changing_page);
/// 当前主页面最多三个横向灰阶条带，其余内容按黑白驱动；无入口许可时忽略。
/// Register at most three horizontal gray bands on the armed main page; other content uses black/white. Ignore without permission.
void display_main_transition_gray_bands(const EpdRect* bands, unsigned count);
/// 书架分页加强白色擦除，保持底栏边界。/ Strengthen shelf-page whitening within the fixed navigation boundary.
void display_main_transition_shelf_page(void);
/// 解除尚未使用的入口许可。/ Disarm unused entry permission.
void display_main_transition_disarm(void);
/// 取消尚未使用的入口；不打断物理波形，无延迟补刷任务。
/// Cancel unused permission without interrupting a physical waveform; no deferred completion exists.
void display_main_transition_cancel(void);
enum EpdDrawError update_display_from_white(EpdiyHighlevelState* hl);
enum EpdDrawError update_display_from_white_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
);
/// 把前缓冲铺白再 GC16 全刷，物理屏回到白底。
/// Paint the front buffer white and GC16 the panel back to white.
enum EpdDrawError update_display_white(EpdiyHighlevelState* hl);
enum EpdDrawError update_display_full(EpdiyHighlevelState* hl);

/// 面板修复：不看 framebuffer 的整屏盲刷，专治刷新中途断电留下的条纹。
/// Panel repair: a blind full-screen drive that ignores the framebuffer, for the stripes a power
/// cut in the middle of a refresh leaves behind.
/// 返回最终整屏输出结果；失败时调用方不能报告成功。/ Return the final full-frame result; callers must not report success on failure.
enum EpdDrawError display_panel_repair(EpdiyHighlevelState* hl);
/// 图片页清除旧光学状态，再驱动完整灰阶。/ Clear the prior optical state and drive the full gray ladder for images.
enum EpdDrawError update_display_image_gray(EpdiyHighlevelState* hl);
enum EpdDrawError update_display_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
);
/// 灰阶图还在屏上时置位：菜单盖上来或离页先刷白，避免从中间灰差分。
/// Set while a gray image is still on panel: wipe to white before the menu or leave so the next update is not a mid-gray differential.
void display_hold_white_exit(bool hold);
bool display_take_white_exit(void);
enum EpdDrawError update_display_area_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
);
/// 动画定稿只驱动真实差异，不强制把区域内所有 GL16 像素重刷。
/// Animation settle drives real differences only instead of forcing every GL16 pixel in the area.
/// 在指定区域驱动全部像素，用于局部定稿和清残影。/ Drive all pixels within an area for local settling and ghost cleanup.
enum EpdDrawError update_display_area_full_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
);
enum EpdDrawError update_display_area_diff_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
);
/// 阅读页可选的真实错相 GL16 翻页；缺相位时回退现有 GL16，失败时恢复基准帧。
/// Optional staggered GL16 reader turn; fall back on missing phases and recover the baseline after failure.
enum EpdDrawError update_display_water_turn(EpdiyHighlevelState* hl, EpdRect area,
                                             e0470_turn_dir_t dir);
/// 动画中间帧使用灰阶波形，但不累计周期清屏计数；动画最终帧走普通刷新出口。
/// Use grayscale for animation strips without advancing periodic cleanup; the final frame uses the normal path.

/// 当前像素时钟。/ Current pixel clock.
int display_pclk_mhz(void);
/// 出现供数不足就退回安全频率并整屏重刷，其它错误码原样忽略。
/// On underrun, drop to the safe clock and full-refresh; other error bits are ignored.
void guard_draw_result(EpdiyHighlevelState* hl, enum EpdDrawError result);

#ifdef __cplusplus
}
#endif
