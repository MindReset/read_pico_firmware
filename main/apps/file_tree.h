/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * Bounded TF-card file and folder operations shared by the file manager.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct { unsigned nodes; uint64_t bytes; } file_tree_info_t;
typedef void (*file_tree_deleted_cb_t)(const char *path, bool directory, void *ctx);

bool file_tree_same_or_below(const char *path, const char *directory);
esp_err_t file_tree_inspect(const char *path, file_tree_info_t *info);
esp_err_t file_tree_copy(const char *source, const char *target, uint64_t free_bytes);
esp_err_t file_tree_move(const char *source, const char *target);
esp_err_t file_tree_delete(const char *path, file_tree_deleted_cb_t callback, void *ctx);
