/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "epd_highlevel.h"

/// 按原比例居中裁切并铺满区域；预览与锁屏共用。
/// Center-crop an SD image at its original aspect ratio to cover preview and lock screen.
bool ui_wallpaper_draw(uint8_t *framebuffer, const char *path, EpdRect area);

/// 居中填满圆角区域并裁切四角，用于设置页头像。
/// Center-crop into a rounded area for the settings avatar.
bool ui_wallpaper_draw_rounded(uint8_t *framebuffer, const char *path, EpdRect area, int radius);

/// 优先读TF成品头像；首次裁切后落卡，绘制无IO且不保留原图。
/// Prefer the processed SD avatar, saving the first crop; painting has no IO or retained source image.
bool ui_wallpaper_prepare_avatar(const char *path, uint8_t *pixels, int side);
/// 只读成品缓存，未命中不解码；供首帧前快速恢复头像。
/// Load only a processed cache without decoding on misses, for fast avatar restoration before the first frame.
bool ui_wallpaper_avatar_cached(const char *path,uint8_t *pixels,int side);
void ui_wallpaper_draw_avatar(uint8_t *framebuffer, const uint8_t *pixels, EpdRect area, int radius);

/// 锁屏专用TF成品缓存，源文件/尺寸/方向/算法版本变化或缓存损坏时重建。
/// SD cache for lock artwork; changed source, geometry, rotation, algorithm revision or corruption rebuilds it.
bool ui_wallpaper_prepare_lock(uint8_t *framebuffer,const char *path);
/// 在最终画面显示成功后、密码层覆盖前调用；复用调用者画布，不新增整帧。
/// Persist after successful presentation, before the PIN overlay, using the caller's unchanged canvas.
void ui_wallpaper_cache_commit(const uint8_t *framebuffer);
