/* SPDX-License-Identifier: Apache-2.0
 * 中文：白屏锁屏反馈上的圆锁，无字库、文件 I/O、动画等待或像素缓存。
 * English: A lock badge on the white feedback frame, without fonts, file I/O, animation waits or pixel caches.
 * 用户修订：取消占位拼贴和分步显影，调用者先清白后局部画锁；最终拼贴绘制器保持。
 * User revision: remove placeholders and staged reveal; the caller whitens before the local lock, retaining final collage artwork.
 */
#include "ui_lock_feedback.h"
#include <stdbool.h>

static bool round_body(int x, int y, int inset) {
    int left = -16 + inset, right = 16 - inset;
    int top = -2 + inset, bottom = 24 - inset, r = 5 - inset;
    if (x < left || x > right || y < top || y > bottom) return false;
    int dx = x < left + r ? left + r - x : x > right - r ? x - right + r : 0;
    int dy = y < top + r ? top + r - y : y > bottom - r ? y - bottom + r : 0;
    return dx * dx + dy * dy <= r * r;
}

EpdRect ui_lock_feedback_draw(uint8_t *fb) {
    const int size = 97, radius = 48;
    int width = epd_rotated_display_width(), height = epd_rotated_display_height();
    if (!fb || width < size || height < size) return (EpdRect){0};
    EpdRect area = {(width - size) / 2, (height - size) / 2, size, size};
    int cx = area.x + radius, cy = area.y + radius;
    for (int y = -radius; y <= radius; ++y) for (int x = -radius; x <= radius; ++x) {
        int circle = x * x + y * y;
        if (circle > radius * radius) continue;
        bool ink = circle >= 45 * 45;
        bool body = round_body(x, y, 0) && !round_body(x, y, 3);
        int arch = x * x + (y + 11) * (y + 11);
        bool shackle = y <= -11 && arch <= 121 && arch >= 64;
        bool upright = y > -11 && y < -1 && ((x >= -11 && x <= -8) || (x >= 8 && x <= 11));
        bool keyhole = x * x + (y - 9) * (y - 9) <= 9 || (x >= -1 && x <= 1 && y >= 9 && y <= 17);
        epd_draw_pixel(cx + x, cy + y, ink || body || shackle || upright || keyhole ? 0x00 : 0xff, fb);
    }
    return area;
}
