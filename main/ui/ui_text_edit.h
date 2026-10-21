/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：共用的 UTF-8 插入光标；不负责键盘、候选词或保存。
 * English: Shared UTF-8 insertion caret; owns neither keyboards, candidates nor persistence.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *text;        ///< 调用方持有的缓冲区 / Caller-owned buffer
    size_t capacity;   ///< 含结尾零字节的容量 / Capacity including the terminator
    size_t cursor;     ///< UTF-8 字符边界上的字节位置 / Byte offset on a UTF-8 boundary
} ui_text_edit_t;

/// 绑定已有文字，光标从末尾开始。/ Bind existing text with the caret at its end.
void ui_text_edit_init(ui_text_edit_t *edit, char *text, size_t capacity);
/// 在光标处插入，容量不足时保持原文。/ Insert at the caret, preserving text on insufficient capacity.
bool ui_text_edit_insert(ui_text_edit_t *edit, const char *text);
/// 删除光标前的完整 UTF-8 字符。/ Delete the complete UTF-8 character before the caret.
bool ui_text_edit_backspace(ui_text_edit_t *edit);
/// 左右移动一个字符，或将字节位置归到字符边界。/ Move by one character or place on a character boundary.
void ui_text_edit_move(ui_text_edit_t *edit, int direction);
void ui_text_edit_place(ui_text_edit_t *edit, size_t position);
/// 返回下一个 UTF-8 字符边界。/ Return the next UTF-8 character boundary.
size_t ui_text_edit_next(const char *text, size_t position);
