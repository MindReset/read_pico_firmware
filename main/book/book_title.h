/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：按图书路径保存用户书名覆盖，不修改原文件和阅读进度。
 * English: Keep per-path display titles without editing book files or progress.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

bool book_title_get(const char *path, char *out, size_t cap);
esp_err_t book_title_set(const char *path, const char *title);
esp_err_t book_title_clear(const char *path);
/// 以实际文件名（去扩展名）生成系统统一显示书名。/ Derive the shared display title from the real filename without its extension.
bool book_title_from_path(const char *path, char *out, size_t cap);
/// 清除导入元数据尾部的已知转换器残留。/ Strip known converter debris at the end of imported metadata.
void book_title_clean_import(char *title);
