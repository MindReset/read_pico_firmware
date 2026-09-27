/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 按书籍路径保存 NVS 进度，不依赖 UI 或 SD。
 * Per-path NVS reading progress, independent of UI and SD.
 *
 * 冻结：文件大小不同不恢复；失败返回调用方并告警，不擦除其他记录。
 * 为无 RTC 的最近阅读排序，v2 使用持久序号；v1 保留续读但排序置零。
 * Frozen: do not restore changed file sizes; report and warn on failures, never erase other records.
 * Recent ordering without an RTC uses persistent v2 sequences; v1 retains resume data with order zero.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/// 调用方须串行化所有进度写入与遗忘；HTTP 退出并 join 后才交回阅读页。
/// Callers serialize progress writes and forget operations; HTTP stops and joins before returning to reading.

typedef struct book_progress_watch book_progress_watch_t;
/// 为待保存进度创建路径监视；内存不足返回 NULL。注册/注销仅在 HTTP 停止并 join 后调用。
/// Watch pending progress for one path; return NULL on allocation failure. Register/unregister only while HTTP is stopped and joined.
book_progress_watch_t* book_progress_watch_create(const char* path);
/// HTTP 可将失效位置位；原子读取，直到注销都不复位。/ HTTP may invalidate; read atomically, never reset before destruction.
bool book_progress_watch_invalidated(const book_progress_watch_t* watch);
/// 移除并释放监视；不可与遗忘并发。/ Remove and free a watch; never run concurrently with forget.
void book_progress_watch_destroy(book_progress_watch_t* watch);

typedef struct {
    /// 文件原始字节数。/ Original file size in bytes.
    uint32_t file_size;
    /// 章节索引。/ Chapter index.
    uint16_t chapter;
    /// 章节内 UTF-8 字节偏移。/ UTF-8 byte offset within the chapter.
    uint32_t byte_off;
    /// 阅读字号。/ Reading font size.
    uint8_t px;
    /// 全书百分比。/ Whole-book percentage.
    uint8_t pct;
    /// 持久阅读顺序；保留旧字段名，v1 记录读取为 0。/ Persistent reading order; legacy field name, v1 records load as zero.
    uint32_t last_open_s;
} book_progress_t;

/// 校验版本、完整路径与文件大小后恢复；失败不改 out。/ Restore after version, full-path and size validation; leave out unchanged on failure.
bool book_progress_load(const char* path, uint32_t size, book_progress_t* out);
/// 保存 v2 进度，忽略输入 last_open_s 并预留持久单调序号；路径最长 287 字节，碰撞拒绝覆盖。
/// Save v2 progress, replacing input last_open_s with a reserved persistent sequence; paths up to 287 bytes, reject collision overwrites.
esp_err_t book_progress_save(const char* path, const book_progress_t* progress);
/// 清除此路径的进度，不删除碰撞记录。/ Clear this path without deleting colliding records.
esp_err_t book_progress_clear(const char* path);
/// 清除此路径进度及匹配的 last；任一步失败返回错误，允许幂等重试，保留碰撞记录。
/// Forget this path's progress and matching last; return any failure, allow idempotent retries, preserve colliding records.
/// 有效路径在首次 NVS 操作前使对应待保存监视失效，失败也不回写旧文件进度。
/// Invalidate matching pending watches before any NVS operation so failures cannot restore an old file's progress.
esp_err_t book_progress_forget(const char* path);
/// 读取最后一本的路径；失败输出空串。/ Read the last book path; output an empty string on failure.
bool book_progress_last_path(char* out, size_t cap);
/// 保存最后路径；空串清除自动恢复入口。/ Save last path; empty string clears automatic restore.
esp_err_t book_progress_set_last_path(const char* path);
