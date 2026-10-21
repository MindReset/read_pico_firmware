/* SPDX-License-Identifier: Apache-2.0
 * 中文：有界首页组件，绘制无分配与文件读取。/ English: Bounded Home components; drawing never allocates or reads files.
 */
#pragma once
#include "ui_kit.h"
#include <time.h>
#define UI_HOME_WIDGET_W 320
#define UI_HOME_WIDGET_H 350
#define UI_HOME_FOLDER_GRAY_BYTES (UI_HOME_WIDGET_W*UI_HOME_WIDGET_H/2)
#define UI_HOME_FOLDER_BW_BYTES (UI_HOME_WIDGET_W*UI_HOME_WIDGET_H/8)
typedef struct {
    const uint8_t *pixels;
    uint16_t width,height,stride;
    bool binary;
} ui_home_cover_t;
/// 临时工作区224000字节；输出最多56000字节，调用者控制保留及释放。/ A 224000-byte temporary workspace and at most 56000-byte output; the caller owns allocation/lifetime.
bool ui_home_folder_compose(uint8_t *out,uint8_t *scratch,const ui_home_cover_t covers[3],unsigned count,bool binary);
void ui_home_folder_draw(uint8_t *fb,const uint8_t *pixels,bool binary,EpdRect bounds);
/// 复用同一曲线与短底座，bounds为组件画布，内部按比例变换。/ Reuse one curved short-pedestal shape, scaled inside component bounds.
void ui_home_clock_draw(uint8_t *fb,EpdRect bounds,const struct tm *date,bool binary);
