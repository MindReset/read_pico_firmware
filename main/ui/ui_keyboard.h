/* SPDX-License-Identifier: Apache-2.0
 * 中文：共用手机式墨水屏键盘，一次只有当前页面使用，退出清除输入状态。
 * English: Shared smartphone-style e-paper keyboard; only the active page owns it, with state cleared on exit.
 */
#pragma once
#include "ui_text_input.h"
typedef enum { UI_KEYBOARD_NONE, UI_KEYBOARD_CHANGED, UI_KEYBOARD_DONE } ui_keyboard_result_t;
/// 绑定编辑缓冲区；九宫格只输入拼音，密码模式只用英文全键盘、数字和符号。
/// Bind the editor; T9 is Pinyin-only, passwords use English QWERTY, digits and symbols.
void ui_keyboard_begin(ui_text_edit_t *edit, bool ascii_only);
/// 清除拼音、候选与编辑指针。/ Clear composition, candidates and the editor pointer.
void ui_keyboard_end(void);
/// 固定 636×510 键盘，纯绘制，使用系统字体。/ Paint a fixed 636x510 keyboard using system typography only.
void ui_keyboard_draw(uint8_t *fb, int top);
/// 松手触发按键；时间参数保留兼容，不再使用英文九宫格连续点按。
/// Activate on release; keep the time argument for compatibility without English T9 multi-tap.
ui_keyboard_result_t ui_keyboard_tap(int x, int y, int top, int64_t now_ms);
/// 按下即绘制轻量反馈，不提交字符；松开或取消恢复。/ Lightweight press feedback without committing a character.
bool ui_keyboard_press(int x, int y, int top, int64_t now_ms);
/// 清除按下反馈并取消连删；返回是否需要局部重绘。/ Clear feedback/repeat; true requests local repaint.
bool ui_keyboard_release(void);
/// 按下删除键时计时；不立即删除，短按仍在松手时处理。/ Arm deletion on press; short taps still delete on release.
void ui_keyboard_hold_start(int x, int y, int top, int64_t now_ms);
/// 松手、滑动、切页或中断取消连续删除。/ Cancel repeat on release, movement, page changes or interruptions.
void ui_keyboard_hold_cancel(void);
/// 单指按住 500ms 后每 120ms 删除一位，优先删拼音；每轮最多删一次，不补发积压。
/// After a 500ms single-finger hold, delete Pinyin first, then text every 120ms; no catch-up bursts.
ui_keyboard_result_t ui_keyboard_hold_tick(bool held, int x, int y, int64_t now_ms);
/// 词栏累计六次变化且无触摸停顿 700ms 后，安排一次局部灰阶整理；离页自动取消。
/// After six word-strip changes and a 700ms touch-free pause, request one local gray settle; exit cancels it.
ui_keyboard_result_t ui_keyboard_idle_tick(bool touching, int64_t now_ms);
/// 尚未确认的拼音不可保存。/ Unconfirmed Pinyin prevents saving.
bool ui_keyboard_pending(void);
/// 左右滑动候选页，边界不循环。/ Swipe candidate pages without wrapping at the edges.
bool ui_keyboard_page(int direction);
typedef struct {
    EpdRect area;       ///< 实际重绘区域，宽为零表示无变化 / Actual painted bounds; zero width means unchanged
    bool layout;        ///< 布局切换用局部灰阶，其余为快速输入反馈 / Local grayscale for layout switches, fast input otherwise
    bool field;         ///< 已重绘输入值或光标 / Input value or caret was repainted
    bool settle;        ///< 仅词栏的全像素 GL16，不使用差分或整屏刷新 / Full-pixel GL16 for the word strip only, never a differential or whole-screen refresh
} ui_keyboard_update_t;
/// 只重绘变化的输入框、拼音、候选或键盘；仅从事件回调调用。
/// Paint changed field, composition, candidates or keyboard only; call from event handlers only.
ui_keyboard_update_t ui_keyboard_update(uint8_t *fb, int top, EpdRect field, int px,
                                        bool masked, const char *suffix, bool force_field);
