/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：点词查译弹窗的外部接口；被 app_book 阅读页调用，不导出 app_desc_t。
 * English: External API of the lookup popup; called by the reader page, exports no app_desc_t.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "epdiy.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 卡片区域（绘制与命中共用）。/ Card rectangle, shared by draw and hit-test.
EpdRect dict_popup_rect(void);
/// 避让锚点：点词所在侧与分半几何开关（v2 §5.2）。须在 dict_popup_open() 之前调用——
/// 开放时按最终卡片宽度折行释义。col=0 点词在左半（卡片弹出到右半），col=1 反之；
/// split=false 时横屏维持铺满（避让规则是双栏特性，单栏不做无谓的行为分裂）。
/// Avoidance anchor: the tapped side and the split-geometry switch (v2 section 5.2).
/// Must be called before dict_popup_open() — the definition wraps to the final card
/// width at open time. col=0 taps on the left half (card pops to the right half) and
/// vice versa; split=false keeps landscape full width (the avoidance is a two-column
/// feature; single column does not split behavior for nothing).
void dict_popup_set_anchor(int col, bool split);
/// 查词并准备弹窗内容；成功返回 true（无词典或未收录返回 false）。
/// Look up the word and stage the content; false when no dictionary or no entry.
bool dict_popup_open(const char* word);
/// 释放内容与词头。/ Release staged content.
void dict_popup_reset(void);
/// 在阅读页内容之上绘制卡片。/ Paint the card over the reader content.
void dict_popup_draw(uint8_t* fb);
/// 「保存」chip 的区域（绘制与命中共用）。/ Rectangle of the save chip, shared by draw and hit-test.
EpdRect dict_popup_save_rect(void);

/// chip 点击的结果。调用方据此决定「关卡片」「留着」还是「删完要重开视图」。
/// What a chip tap did; the caller decides whether to close the card, keep it, or
/// re-open the vocabulary view after a removal.
typedef enum {
    DICT_POPUP_TAP_MISSED,  ///< 没点到 chip，调用方照旧关卡片 / not the chip; close as usual
    DICT_POPUP_TAP_KEPT,    ///< 命中并切换了状态，卡片保持打开 / toggled; keep the card open
    DICT_POPUP_TAP_REMOVED, ///< 命中并从生词本移除了一条 / removed one entry
} dict_popup_tap_t;

/// 点击落在 chip 上时切换生词本保存状态。未命中由调用方关卡片。
/// Toggle the vocabulary-book entry when the tap lands on the chip; a miss is the
/// caller's job to close.
dict_popup_tap_t dict_popup_tap(uint16_t x, uint16_t y);

#ifdef __cplusplus
}
#endif
