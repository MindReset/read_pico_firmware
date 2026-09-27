/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：位图类型替身。/ English: Bitmap type shim.
 * 冻结：仅用于测试。/ Frozen: Tests only.
 */
#pragma once
#include <stdint.h>
typedef struct { int width, height; const uint8_t *bits; } ui_fallback_bmp_t;
extern const ui_fallback_bmp_t ui_fb_insert, ui_fb_remount, ui_fb_confirm, ui_fb_format;
