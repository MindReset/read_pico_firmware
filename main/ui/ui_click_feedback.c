/* SPDX-License-Identifier: Apache-2.0
 * 中文：底栏图标放大和圆形返回键轻微下沉，只暂存一个控件真实像素。
 * English: Enlarge navigation icons and gently depress round backs, retaining actual pixels for one control only.
 * 冻结：无整帧缓存或延迟回放；释放、移出、读错时恢复，整页/切页时丢弃旧缓存。
 * Frozen: No whole-frame storage or deferred replay. Restore on release, move-out or read error; discard on a new paint or navigation.
 * 用户修订：普通、快刷、水波纹共用底栏反馈，保留灰底及文字；仅重绘图标盒并推真实变化边界，避免矩形白框。
 * User revision: ordinary, fast and water share navigation feedback, retaining gray backgrounds and labels; repaint only the icon box and present actual change bounds to avoid white rectangles.
 */
#include "ui_click_feedback.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "settings.h"
#include "ui_nav_layout.h"
typedef struct { EpdRect area; ui_click_kind_t kind; ui_icon_t icon; } click_control_t;
static click_control_t s_controls[12], s_active;
static unsigned s_count;
static uint8_t *s_pixels;
static uint8_t *s_frame;
static int s_x, s_y;
bool ui_click_feedback_active(void) { return s_pixels != NULL; }
void ui_click_feedback_reset(void) {
    heap_caps_free(s_pixels); s_pixels = NULL; s_count = 0; s_frame = NULL;
}
void ui_click_feedback_begin(uint8_t *fb) { ui_click_feedback_reset(); s_frame = fb; }
void ui_click_feedback_register(uint8_t *fb, EpdRect rect, ui_click_kind_t kind, ui_icon_t icon) {
    if (fb != s_frame || !fb) return;
    if (rect.x < 0 || rect.y < 0 || rect.width <= 0 || rect.height <= 0 ||
        rect.width > 64 || rect.height > 64 || rect.x + rect.width > epd_rotated_display_width() ||
        rect.y + rect.height > epd_rotated_display_height()) return;
    for (unsigned i = 0; i < s_count; ++i) if (!memcmp(&s_controls[i].area, &rect, sizeof(rect))) {
        s_controls[i] = (click_control_t){rect, kind, icon}; return;
    }
    if (s_count < 12) s_controls[s_count++] = (click_control_t){rect, kind, icon};
}
static uint8_t read_pixel(uint8_t *fb, int x, int y) {
    int px = x, py = y;
    switch (epd_get_rotation()) {
        case EPD_ROT_PORTRAIT: px = epd_width() - y - 1; py = x; break;
        case EPD_ROT_INVERTED_LANDSCAPE: px = epd_width() - x - 1; py = epd_height() - y - 1; break;
        case EPD_ROT_INVERTED_PORTRAIT: px = y; py = epd_height() - x - 1; break;
        default: break;
    }
    return epd_get_pixel(px, py, epd_width(), epd_height(), fb);
}
// 缓存边界不等于推屏边界；未变的灰底交给等灰保持，文字和横条不参与动效。
// Cache bounds are not presentation bounds; hold unchanged grays and exclude labels and the marker from feedback.
static EpdRect changed_area(uint8_t *fb) {
    EpdRect r = s_active.area;
    int left = r.width, top = r.height, right = -1, bottom = -1;
    for (int y = 0; y < r.height; ++y) for (int x = 0; x < r.width; ++x) {
        if (read_pixel(fb, r.x + x, r.y + y) == s_pixels[y * r.width + x]) continue;
        if (x < left) left = x;
        if (y < top) top = y;
        if (x > right) right = x;
        if (y > bottom) bottom = y;
    }
    return right < 0 ? r : (EpdRect){r.x + left, r.y + top, right - left + 1, bottom - top + 1};
}
bool ui_click_feedback_release(uint8_t *fb, EpdRect *area) {
    if (!s_pixels) return false;
    *area = changed_area(fb);
    EpdRect saved = s_active.area;
    for (int y = 0; y < saved.height; ++y) for (int x = 0; x < saved.width; ++x)
        epd_draw_pixel(saved.x + x, saved.y + y, s_pixels[y * saved.width + x], fb);
    heap_caps_free(s_pixels); s_pixels = NULL;
    return true;
}
bool ui_click_feedback_cancel_at(int x, int y) {
    bool outside = s_active.kind == UI_CLICK_NAV
        ? x < 0 || x >= UI_LOCK_WIDTH || y < UI_NAV_TOUCH_TOP || y >= UI_LOCK_HEIGHT ||
          x / (UI_LOCK_WIDTH / 4) != s_x / (UI_LOCK_WIDTH / 4)
        : x < s_active.area.x || x >= s_active.area.x + s_active.area.width ||
          y < s_active.area.y || y >= s_active.area.y + s_active.area.height;
    return s_pixels && (outside ||
        x - s_x > UI_TOUCH_SLOP_PX || s_x - x > UI_TOUCH_SLOP_PX ||
        y - s_y > UI_TOUCH_SLOP_PX || s_y - y > UI_TOUCH_SLOP_PX);
}
bool ui_click_feedback_press(uint8_t *fb, int x, int y, EpdRect *area) {
    if (s_pixels) return false;
    for (unsigned i = 0; i < s_count; ++i) {
        EpdRect r = s_controls[i].area;
        bool hit = s_controls[i].kind == UI_CLICK_NAV
            ? y >= UI_NAV_TOUCH_TOP && y < UI_LOCK_HEIGHT && x >= 0 && x < UI_LOCK_WIDTH &&
              x / (UI_LOCK_WIDTH / 4) == (r.x + r.width / 2) / (UI_LOCK_WIDTH / 4)
            : x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
        if (!hit) continue;
        s_pixels = heap_caps_malloc((size_t)r.width * r.height, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_pixels) return false;
        s_active = s_controls[i]; s_x = x; s_y = y; *area = r;
        for (int yy = 0; yy < r.height; ++yy) for (int xx = 0; xx < r.width; ++xx)
            s_pixels[yy * r.width + xx] = read_pixel(fb, r.x + xx, r.y + yy);
        int cx = r.x + r.width / 2, cy = r.y + r.height / 2;
        if (s_active.kind == UI_CLICK_BACK || s_active.kind == UI_CLICK_BACK_SMALL) {
            epd_fill_rect(r, s_pixels[0], fb);
            // 小题头返回圈保持原44px和中心，按压只改底色；其他返回圈保留原动效。
            // Small header backs retain their 44px bounds and center, changing fill only; retain other back feedback.
            bool small=s_active.kind==UI_CLICK_BACK_SMALL;
            int size=small?44:50,offset=small?0:1;
            EpdRect circle={cx-size/2,cy-size/2+offset,size,size};
            ui_fill_round_rect(fb,circle,size/2,0xd0);
            ui_draw_round_rect(fb,circle,size/2,0x40);
            ui_draw_icon(fb,cx,cy+offset,small?38:42,s_active.icon,UI_GRAY_BLACK);
            if (app_settings_main_fast_refresh()) ui_image_bw_rect(fb, r);
        } else {
            // 底栏始终保留原灰底，不把整个按压缓存转成黑白点阵。
            // Retain the original footer gray in every mode rather than binarizing the entire pressed cache.
            // 放大笔画从32列对齐的横条扫描边界下方开始，避开横条及底部文字。
            // Enlarged ink starts below the marker's aligned 32-column scan boundary, clear of its marker and labels.
            epd_fill_rect((EpdRect){cx - 25, cy - 23, 50, 50}, s_pixels[0], fb);
            ui_draw_icon(fb, cx, cy + 2, 50, s_active.icon, UI_GRAY_BLACK);
        }
        *area = changed_area(fb);
        return true;
    }
    return false;
}
