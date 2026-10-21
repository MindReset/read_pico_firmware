/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界 UTF-8 编辑；移动与退格不切断中文或四字节字符。
 * English: Bounded UTF-8 editing; movement and backspace keep Chinese and four-byte characters intact.
 */
#include "ui_text_edit.h"
#include <string.h>

static size_t edit_length(const ui_text_edit_t *edit) {
    if (!edit || !edit->text || !edit->capacity) return 0;
    size_t length = 0;
    while (length + 1 < edit->capacity && edit->text[length]) ++length;
    return length;
}

void ui_text_edit_place(ui_text_edit_t *edit, size_t position) {
    if (!edit || !edit->text || !edit->capacity) return;
    size_t length = edit_length(edit);
    if (position > length) position = length;
    while (position && ((unsigned char)edit->text[position] & 0xc0) == 0x80) --position;
    edit->cursor = position;
}

void ui_text_edit_init(ui_text_edit_t *edit, char *text, size_t capacity) {
    if (!edit) return;
    *edit = (ui_text_edit_t){.text = text, .capacity = capacity};
    if (!text || !capacity) return;
    text[capacity - 1] = 0;
    ui_text_edit_place(edit, edit_length(edit));
}

size_t ui_text_edit_next(const char *text, size_t position) {
    if (!text[position]) return position;
    ++position;
    while (text[position] && ((unsigned char)text[position] & 0xc0) == 0x80) ++position;
    return position;
}

void ui_text_edit_move(ui_text_edit_t *edit, int direction) {
    if (!edit || !edit->text || !edit->capacity) return;
    ui_text_edit_place(edit, edit->cursor);
    if (direction > 0) edit->cursor = ui_text_edit_next(edit->text, edit->cursor);
    else if (direction < 0 && edit->cursor) ui_text_edit_place(edit, edit->cursor - 1);
}

bool ui_text_edit_insert(ui_text_edit_t *edit, const char *text) {
    if (!edit || !edit->text || !edit->capacity || !text) return false;
    size_t length = edit_length(edit), added = strlen(text);
    if (added > edit->capacity - 1 - length) return false;
    ui_text_edit_place(edit, edit->cursor);
    memmove(edit->text + edit->cursor + added, edit->text + edit->cursor,
            length - edit->cursor + 1);
    memcpy(edit->text + edit->cursor, text, added);
    edit->cursor += added;
    return true;
}

bool ui_text_edit_backspace(ui_text_edit_t *edit) {
    if (!edit || !edit->text || !edit->capacity) return false;
    ui_text_edit_place(edit, edit->cursor);
    if (!edit->cursor) return false;
    size_t old = edit->cursor;
    ui_text_edit_place(edit, old - 1);
    memmove(edit->text + edit->cursor, edit->text + old, edit_length(edit) - old + 1);
    return true;
}
