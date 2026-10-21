/* SPDX-License-Identifier: Apache-2.0
 * 中文：从已解析正文缓存完整短句，不开书或改进度。/ English: Cache a complete sentence from already parsed text without opening books or changing progress.
 */
#pragma once
#include "html_text.h"
#define BOOK_EXCERPT_CAP 240
bool book_excerpt_load(const char *path, char out[BOOK_EXCERPT_CAP]);
void book_excerpt_offer(const char *path, const html_text_t *text);
