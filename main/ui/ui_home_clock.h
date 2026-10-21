/* SPDX-License-Identifier: Apache-2.0
 * 中文：已确认首页时钟与同款时间页，静态表盘只读，绘制不分配内存。
 * English: Approved Home clocks and their matching time pages; read-only faces and allocation-free drawing.
 */
#pragma once
#include "ui_home_widget.h"
typedef enum {
    UI_HOME_CLOCK_PS1995, UI_HOME_CLOCK_ASTERISK,
    UI_HOME_CLOCK_MOON, UI_HOME_CLOCK_KEHAI, UI_HOME_CLOCK_DALI, UI_HOME_CLOCK_CAT, UI_HOME_CLOCK_HOUSE, UI_HOME_CLOCK_COUNT
} ui_home_clock_style_t;
// 可选的调用方缓冲保存两张小尺寸表盘底图，以分钟为键，不缓存另一份字体。
// Optional caller-owned scratch holds two small, minute-keyed base rasters, without another font.
#define UI_HOME_CLOCK_BASE_CACHE_BYTES (2u*UI_HOME_WIDGET_W*UI_HOME_WIDGET_H/2u)
/// 在绘制外绑定缓冲；绘制过程不分配内存、不读文件。/ Bind outside drawing; rendering never allocates or reads files.
void ui_home_clock_bind_base_cache(uint8_t *memory,size_t bytes);
const char *ui_home_clock_name(ui_home_clock_style_t style);
void ui_home_clock_style_draw(uint8_t *fb,EpdRect bounds,const struct tm *date,bool binary,ui_home_clock_style_t style);
/// 秒针旧/新位置的有界区域，用于恢复磨砂底色并局部移动圆点。/ Bounded old/new second positions for local frosted-background restoration.
EpdRect ui_home_clock_second_area(EpdRect bounds,const struct tm *before,const struct tm *after);
void ui_home_clock_style_draw_region(uint8_t *fb,EpdRect bounds,const struct tm *date,bool binary,ui_home_clock_style_t style,EpdRect clip);
/// 圆点每圈600小步，按实际时间取最新相位，跳过旧帧而不排队。
/// The dot uses 600 steps per circle; choose the current time phase and skip stale frames without queuing.
void ui_home_clock_phase_draw_region(uint8_t *fb,EpdRect bounds,const struct tm *date,bool binary,ui_home_clock_style_t style,EpdRect clip,unsigned phase);
EpdRect ui_home_clock_phase_area(EpdRect bounds,unsigned before,unsigned after);

/// 仅动态部件使用短局刷；静态表盘保持不变。/ Short partial updates move dynamic parts only.
bool ui_home_clock_has_motion(ui_home_clock_style_t style);
EpdRect ui_home_clock_motion_area(EpdRect bounds,unsigned before,unsigned after,ui_home_clock_style_t style);

/// 浅灰圆周轨迹最多一圈，保持经过的位置；仅活动圆点使用深灰。
/// Keep at most one lap of a light-gray circular trail, with a dark moving dot.
void ui_home_clock_trail_draw_region(uint8_t *fb,EpdRect bounds,const struct tm *date,bool binary,ui_home_clock_style_t style,EpdRect clip,unsigned phase,unsigned trail_start,unsigned trail_steps);
