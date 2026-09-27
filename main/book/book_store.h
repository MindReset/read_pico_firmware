/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 书源根目录、挂载与容量策略，不包含界面。
 * Book roots, mounting and capacity policy without UI.
 *
 * 冻结：SD 优先；内置存储单文件最多 1 MiB，仅首次使用挂载。
 * Frozen: prefer SD; flash files are limited to 1 MiB and mounted lazily.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define BOOK_STORE_FLASH_FILE_MAX (1024U * 1024U)
#define BOOK_STORE_PATH_MAX 288

typedef struct {
    /// 书目录绝对路径。/ Absolute book directory path.
    char path[32];
    /// 内置存储标记。/ Internal-storage marker.
    bool is_flash;
} book_store_root_t;

/// 返回可用目录，SD 在前；至少一个可用即成功。可能首次格式化内置分区。
/// Return usable roots, SD first; succeeds if any is usable. May initially format flash.
esp_err_t book_store_roots(book_store_root_t out[2], int* n);
/// 最近一次 roots 扫描有来源失败；未插 SD 不算失败，成功返回也可能为 true。
/// Whether the last roots scan lost a source; absent SD is not failure, and a successful call may still be degraded.
bool book_store_roots_degraded(void);
/// 选择上传目录；SD 已挂载时不回退掩盖其目录错误。/ Select upload root; do not hide a mounted SD directory error.
esp_err_t book_store_upload_root(book_store_root_t* out);
/// 单文件限制；SD 为 SIZE_MAX。/ File limit; SIZE_MAX for SD.
size_t book_store_file_limit(const book_store_root_t* root);
/// 查询实时剩余容量，错误返回 0。/ Query current free capacity, returning zero on error.
uint64_t book_store_free_bytes(const book_store_root_t* root);
/// 仅查询内置分区是否已挂载，不触发挂载。/ Inspect flash mount state without mounting.
bool book_store_flash_ready(void);
/// 仅删除已挂载书根内的直接 TXT/EPUB 普通文件，然后清对应进度及 last。
/// Delete only a direct regular TXT/EPUB file in a mounted book root, then forget its progress and last path.
/// 先清同名普通覆盖备份；仅有备份时先恢复再删除，失败不声称文件已删。
/// Clear matching regular replacement backups first; restore backup-only files before deletion and report failures as not removed.
/// removed 为文件已删或已不存在；此时 NVS 仍可能失败，可再次调用清理。调用方负责关闭句柄及通知版本。
/// removed means deleted or already absent; NVS may still fail and cleanup can be retried. Caller closes handles and notifies revision.
esp_err_t book_store_delete(const char* path, bool* removed);
/// 上传结束后由UI线程通知书架重扫；不修改已保存的阅读进度。
/// Notify a shelf rescan from the UI thread after upload without changing saved progress.
void book_store_notify_changed(void);
/// 当前内容版本，仅由UI线程读写。/ Content revision, accessed only by the UI thread.
unsigned book_store_revision(void);
