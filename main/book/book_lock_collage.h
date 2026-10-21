/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书架拼贴锁屏，使用真实封面和当前系统字体；缓存放在 TF 卡。
 * English: Library collage lock with real covers and the active system face; cache on SD.
 * 冻结：只由锁屏/预生成入口调用，字体上下文由调用者管理，不更改阅读书源。
 * Frozen: Call only at lock/preparation boundaries; the caller owns font context, never change the reader source.
 * 用户修订：锁屏入口可延后整帧落卡，画布在提交前必须保持不变；其他调用保留同步缓存。
 * User revision: lock entry may defer frame persistence; keep the borrowed framebuffer intact until commit. Other calls cache synchronously.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/// 画到完整画布；低内存、无卡或封面错误时安全退化，临时像素均在返回前释放。
/// Draw a complete frame; degrade safely on OOM, missing media or covers and free all temporary pixels.
bool book_lock_collage_draw(uint8_t *framebuffer);

/// 准备锁屏而不写新整帧缓存；不保留封面工作区，只借用此画布直到提交或下一次绘制。
/// Prepare without persisting a new frame; retain no cover workspace, borrowing this frame until commit or the next draw.
bool book_lock_collage_prepare(uint8_t *framebuffer);
/// 在显示完成、画布仍未改动时提交一次；失败或指针不匹配时丢弃，不影响已显示的锁屏。
/// Commit once after presentation while pixels are intact; I/O failure or a mismatched pointer leaves displayed art unaffected.
void book_lock_collage_cache_commit(const uint8_t *framebuffer);

/// 书架空闲时预生成一本拼贴封面到TF；不绘屏、不加载第二份字体、不保留像素。
/// Warm one collage cover on SD while the shelf is idle; no display, second font or retained pixels.
bool book_lock_collage_warm_cover(const char *path);

/// 换卡/书库隐藏设置变化时丢弃小型目录索引；不改TF成品画面。/ Discard the small catalogue index on media/visibility changes, preserving completed TF artwork.
void book_lock_collage_invalidate(void);
