/* SPDX-License-Identifier: Apache-2.0
 * 中文：全宽浅顶部层，下圆角与四个无文字圆钮；三个主页模式统一灰阶和清晰边框。
 * English: A shallow full-width top layer with rounded bottom corners and four unlabeled circles; all main modes share grayscale and clear borders.
 * 冻结：不访问硬件、设置或刷新，不申请帧缓存；只在展开/状态变化后由主循环调用绘图。
 * Frozen: No hardware, settings, presentation or frame allocations; the loop draws only on opening or state changes.
 */
#include "ui_quick_menu.h"
#include <stdlib.h>
#include <string.h>
#include "ui_kit.h"
#include "ui_nav.h"
#include "ui_image_dither.h"

static int center_x(int button) { return (button * 2 + 1) * UI_LOCK_WIDTH / 8; }

// 根据当前旋转读取逻辑像素；和图形接口相同，画点保持半字节邻居。
// Read logical pixels using the current rotation; drawing preserves the neighboring nibble like the graphics API.
static uint8_t pixel(uint8_t *fb, int x, int y) {
    int px, py;
    switch (epd_get_rotation()) {
        case EPD_ROT_PORTRAIT: px = epd_width() - y - 1; py = x; break;
        case EPD_ROT_INVERTED_LANDSCAPE: px = epd_width() - x - 1; py = epd_height() - y - 1; break;
        case EPD_ROT_INVERTED_PORTRAIT: px = y; py = epd_height() - x - 1; break;
        default: px = x; py = y; break;
    }
    return (epd_get_pixel(px, py, epd_width(), epd_height(), fb) >> 4) * 17u;
}

static bool layer_pixel(int x, int y, int width) {
    const int r = 56;
    if (y < UI_QUICK_HEIGHT - r || (x >= r && x < width - r)) return true;
    int dx = x < r ? x - r : x - (width - r - 1);
    int dy = y - (UI_QUICK_HEIGHT - r - 1);
    return dx * dx + dy * dy < r * r;
}

// 44px锁形沿用细线几何，圆角锁体与上半圆锁梁；不引入生成图标资产。
// A 44px lock follows the thin-line geometry, with a rounded body and arched shackle; no new generated icon asset is needed.
static void lock_icon(uint8_t *fb, int cx, int cy) {
    for (int stroke = 0; stroke < 3; ++stroke) {
        ui_draw_round_rect(fb, (EpdRect){cx - 14 + stroke, cy - 1 + stroke,
                                      28 - stroke * 2, 23 - stroke * 2}, 4 - stroke, UI_GRAY_BLACK);
        int radius = 9 - stroke;
        for (int x = -radius; x <= radius; ++x) {
            int y = 0;
            while ((y + 1) * (y + 1) + x * x <= radius * radius) ++y;
            epd_draw_pixel(cx + x, cy - 10 - y, UI_GRAY_BLACK, fb);
            if (abs(x) == radius) epd_draw_line(cx + x, cy - 10, cx + x, cy - 1, UI_GRAY_BLACK, fb);
        }
    }
    ui_fill_rect_fast((EpdRect){cx - 1, cy + 8, 3, 7}, UI_GRAY_BLACK, fb);
}

void ui_quick_menu_draw(uint8_t *fb, bool wifi, bool bluetooth) {
    if (!fb) return;
    const int width = epd_rotated_display_width();
    const int height = epd_rotated_display_height();
    // 每个像素只采一次原图，直接合成灰阶，不让主页快刷模式把菜单变成点阵。
    // Sample each underlay pixel once and compose grays directly, independently of fast main-page rendering.
    for (int y = 0; y < UI_QUICK_HEIGHT && y < height; ++y) for (int x = 0; x < width; ++x) {
        if (!layer_pixel(x, y, width)) continue;
        unsigned tint = 238;
        for (int i = 0; i < 4; ++i) {
            int dx = x - center_x(i), dy = y - UI_QUICK_CIRCLE_Y;
            if (dx * dx + dy * dy <= UI_QUICK_RADIUS * UI_QUICK_RADIUS) {
                tint = (i == 0 && wifi) || (i == 1 && bluetooth) ? 176 : 244;
                break;
            }
        }
        uint8_t tone = (uint8_t)((pixel(fb, x, y) * 40u + tint * 216u + 128u) / 256u);
        epd_draw_pixel(x, y, tone, fb);
    }
    // 状态栏留清晰白底，沿用系统时间/签名/电池；四图标与底栏共用Lucide细线44px。
    // Keep the system time/signature/battery legible on white; share navigation's 44px Lucide line icons.
    ui_fill_rect_fast((EpdRect){0, 0, width, 76}, UI_GRAY_WHITE, fb);
    ui_nav_status(fb);
    static const ui_icon_t icons[] = {UI_ICON_WIFI, UI_ICON_BLUETOOTH, UI_ICON_REFRESH_CW};
    for (int i = 0; i < 4; ++i) {
        int cx = center_x(i);
        epd_draw_circle(cx, UI_QUICK_CIRCLE_Y, UI_QUICK_RADIUS, 0x38, fb);
        epd_draw_circle(cx, UI_QUICK_CIRCLE_Y, UI_QUICK_RADIUS - 1, 0x38, fb);
        if (i < 3) ui_draw_icon(fb, cx, UI_QUICK_CIRCLE_Y, 44, icons[i], UI_GRAY_BLACK);
        else lock_icon(fb, cx, UI_QUICK_CIRCLE_Y);
        if ((i == 0 && wifi) || (i == 1 && bluetooth)) {
            epd_fill_circle(cx + 30, UI_QUICK_CIRCLE_Y - 30, 5, UI_GRAY_WHITE, fb);
            epd_draw_circle(cx + 30, UI_QUICK_CIRCLE_Y - 30, 5, 0x50, fb);
        }
    }
    ui_fill_round_rect(fb, (EpdRect){width / 2 - 26, 240, 52, 6}, 3, 0x70);
    // 只画遮罩内的双像素轮廓，顶部直角、底部圆角，不越过下层边界。
    // Draw a two-pixel outline within the mask: square top, rounded bottom, no underlay leakage.
    for (int y = 0; y < UI_QUICK_HEIGHT && y < height; ++y) for (int x = 0; x < width; ++x) {
        if (layer_pixel(x, y, width) && (x < 2 || x >= width - 2 || y < 2 ||
            y >= UI_QUICK_HEIGHT - 2 || !layer_pixel(x - 2, y, width) ||
            !layer_pixel(x + 2, y, width) || !layer_pixel(x, y + 2, width)))
            epd_draw_pixel(x, y, 0x38, fb);
    }
}
