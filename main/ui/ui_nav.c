/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：绘制与命中四栏导航；细线图标分别表示首页、书架、文件和设置，图形来自 Lucide。
 * English: Draw and hit-test the four-tab navigation with thin-line home, books, files and settings icons from Lucide.
 * 用户增加点击反馈：共享返回键和底栏图标登记局部命中，主循环只在按下和复位推该控件。
 * User adds click feedback: register local hits for shared backs and navigation icons; the loop presents only that control on press and restoration.
 * 用户修订：所有模式把内容扫描截止到分隔线之间的8行固定为白色留白；滚动不能把列表画进这条扫描保护带。
 * User revision: every mode keeps the eight rows between content scans and the divider blank white; scrolling must not paint list content into this guard.
 * 用户修订：状态栏签名按系统字体和内建界面字体的共同安全宽度截断，阅读页复用小型字宽表，不重载系统字体。
 * User revision: fit status signatures to widths safe for the system and built-in UI faces; reading reuses a small metrics table without reloading the system font.
 */
#include "ui_nav.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "app_registry.h"
#include "read_pico_pmu.h"
#include "read_pico_pmu_protocol.h"
#include "read_pico_transfer.h"
#include "ui_kit.h"
#include "settings.h"
#include "ui_click_feedback.h"
#include "ttf_font.h"
#include "ui_font.h"

// 底栏图标盒、返回键箭头、充电闪电的边长。
// Box sizes for the bar icons, the back chevron and the charging bolt.
#define UI_NAV_TAB_ICON_PX 44
#define UI_NAV_BACK_ICON_PX 38
#define UI_NAV_BOLT_ICON_PX 26
#define UI_NAV_NETWORK_ICON_PX 28
#define UI_STATUS_TIME_PX 28
#define UI_STATUS_PERCENT_PX 28
#define UI_STATUS_SIGNATURE_PX 26

static bool s_status_reading;
static struct {
    bool ready;
    char signature[96], face[TTF_FONT_PATH_MAX], chosen[TTF_FONT_PATH_MAX];
    uint16_t widths[96], digits[10], colon, percent, dash;
} s_status_metrics;
_Static_assert(UI_STATUS_TIME_PX == UI_STATUS_PERCENT_PX, "status numbers share one native metrics cache");
_Static_assert(sizeof(s_status_metrics) <= 1024, "status metrics never retain a second font or bitmap");

static int status_safe_width(int px, const char *text, const char *sample) {
    int active = ui_text_fixed_context_width_px(px, text, sample);
    int builtin = ui_font_has_text(text) ? ui_font_text_width_px(px, text) : 0;
    return active > builtin ? active : builtin;
}

static void status_prepare_metrics(void) {
    if (s_status_reading) return;
    const char *signature = app_settings_status_signature();
    const char *face = ttf_font_path(), *chosen = app_settings_system_font_path();
    if (s_status_metrics.ready && !strcmp(signature, s_status_metrics.signature) &&
        !strcmp(face, s_status_metrics.face) && !strcmp(chosen, s_status_metrics.chosen)) return;
    memset(&s_status_metrics, 0, sizeof(s_status_metrics));
    snprintf(s_status_metrics.signature, sizeof(s_status_metrics.signature), "%s", signature);
    snprintf(s_status_metrics.face, sizeof(s_status_metrics.face), "%s", face);
    snprintf(s_status_metrics.chosen, sizeof(s_status_metrics.chosen), "%s", chosen);
    char prefix[96] = {0};
    const size_t length = strlen(s_status_metrics.signature);
    for (size_t n = 1; n <= length; ++n) {
        prefix[n - 1] = signature[n - 1];
        if (n < length && ((unsigned char)signature[n] & 0xc0) == 0x80) continue;
        s_status_metrics.widths[n] = status_safe_width(UI_STATUS_SIGNATURE_PX, prefix, signature);
    }
    for (int digit = 0; digit < 10; ++digit) {
        char text[] = {(char)('0' + digit), 0};
        s_status_metrics.digits[digit] = status_safe_width(UI_STATUS_TIME_PX, text, "0123456789:%-");
    }
    s_status_metrics.colon = status_safe_width(UI_STATUS_TIME_PX, ":", "0123456789:%-");
    s_status_metrics.percent = status_safe_width(UI_STATUS_PERCENT_PX, "%", "0123456789:%-");
    s_status_metrics.dash = status_safe_width(UI_STATUS_PERCENT_PX, "-", "0123456789:%-");
    s_status_metrics.ready = true;
}

void ui_nav_status_font_context(bool reading, bool face_changed) {
    s_status_reading = reading;
    if (!reading && face_changed) s_status_metrics.ready = false;
    status_prepare_metrics();
}

static int status_number_width(const char *text) {
    if (!s_status_metrics.ready) return ui_text_fixed_width_px(UI_STATUS_TIME_PX, text);
    int width = 0;
    for (; *text; ++text) width += *text >= '0' && *text <= '9' ? s_status_metrics.digits[*text - '0']
        : *text == ':' ? s_status_metrics.colon : *text == '%' ? s_status_metrics.percent : s_status_metrics.dash;
    return width;
}

static void status_fit_signature(char *text, int width) {
    bool cached = s_status_metrics.ready && !strcmp(text, s_status_metrics.signature);
    size_t n = strlen(text);
    while (n && (cached ? s_status_metrics.widths[n]
                       : ui_text_fixed_context_width_px(UI_STATUS_SIGNATURE_PX, text, app_settings_status_signature())) > width) {
        --n;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}

static const char *const labels[] = {"首页", "书架", "文件管理", "设置"};

void ui_nav_back(uint8_t *fb, int x, int y) {
    ui_click_feedback_register(fb, (EpdRect){x - 10, y + 3, 64, 64}, UI_CLICK_BACK_SMALL, UI_ICON_CHEVRON_LEFT);
    // 参考系统题头缩小返回键，中心仍保持在 y=113。/ Match the smaller reference back control while retaining the title centerline.
    ui_fill_round_rect(fb, (EpdRect){x, y + 13, 44, 44}, 22, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, (EpdRect){x, y + 13, 44, 44}, 22, UI_NAV_BACK_BORDER_GRAY);
    ui_draw_round_rect(fb, (EpdRect){x + 1, y + 14, 42, 42}, 21, UI_NAV_BACK_BORDER_GRAY);
    // Lucide chevron-left 的笔画占画布 14/24 高，取 38 像素复现原来 22 像素的视觉高度。
    // Lucide chevron-left spans 14/24 of its canvas, so 38 px reproduces the old 22 px visual height.
    ui_draw_icon(fb, x + 22, y + 35, UI_NAV_BACK_ICON_PX, UI_ICON_CHEVRON_LEFT, UI_GRAY_BLACK);
}

void ui_nav_wifi_icon(uint8_t *fb, int cx, int cy, int size, uint8_t gray) {
    if (!fb || size < 16 || size > 40) return;
    ui_draw_icon(fb, cx, cy, size, UI_ICON_WIFI, gray);
}

static void icon(uint8_t *fb, int tab, int cx, int y) {
    // 四个底栏图标固定用 Lucide 细线原图，线宽与状态栏一致；y 是图标盒顶边。
    // The four bar icons stay on upstream Lucide line art at the status-bar stroke weight; y is the box top.
    static const ui_icon_t tabs[4] = {
        UI_ICON_HOUSE, UI_ICON_LIBRARY_BIG,
        UI_ICON_FOLDER_OPEN, UI_ICON_SLIDERS_HORIZONTAL,
    };
    if (tab < 0 || tab >= 4) return;
    ui_click_feedback_register(fb, (EpdRect){cx - 30, y - 12, 60, 60}, UI_CLICK_NAV, tabs[tab]);
    ui_draw_icon(fb, cx, y + 18, UI_NAV_TAB_ICON_PX, tabs[tab], UI_GRAY_BLACK);
}

void ui_nav_draw(uint8_t *fb, int active) {
    ui_fill_rect_fast((EpdRect){0, UI_NAV_TOP, UI_LOCK_WIDTH, UI_LOCK_HEIGHT - UI_NAV_TOP}, 0xe0, fb);
    ui_hairline(fb, UI_NAV_TOP, 0, UI_LOCK_WIDTH, UI_GRAY_LIGHT);
    ui_fill_rect_fast((EpdRect){0, UI_NAV_REFRESH_END, UI_LOCK_WIDTH, UI_NAV_TOP - UI_NAV_REFRESH_END}, 0xf0, fb);
    if (app_settings_main_refresh_mode() != APP_MAIN_REFRESH_NORMAL) {
        // 快刷内容边界预留空白；横条在白底上单独短刷，图标和文字保持固定。
        // Reserve a blank fast-refresh boundary; short-update the marker on white while retaining icons and text.
        ui_fill_rect_fast((EpdRect){0, UI_NAV_MARKER_TOP, UI_LOCK_WIDTH, UI_NAV_MARKER_BOTTOM - UI_NAV_MARKER_TOP}, UI_GRAY_WHITE, fb);
    }
    for (int i = 0; i < 4; ++i) {
        int center = (i * 2 + 1) * UI_LOCK_WIDTH / 8;
        if (i == active) ui_fill_rect_fast((EpdRect){center - 28, UI_NAV_TOP + 1, 56, 5}, UI_GRAY_BLACK, fb);
        icon(fb, i, center, UI_NAV_TOP + 27);
        ui_text(fb, center, UI_NAV_TOP + 74, 17, labels[i], EPD_DRAW_ALIGN_CENTER, false);
    }
}

static time_t status_time(void) {
    time_t now = time(NULL);
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    if (now < 1704067200 && pmu && pmu->time_synced && pmu->unix_sec >= 1704067200)
        now = pmu->unix_sec;
    return now;
}
int64_t ui_nav_clock_minute(void) {
    time_t now = status_time();
    return now >= 1704067200 ? (int64_t)now / 60 : -1;
}
static void clock_text(char *clock, size_t capacity) {
    time_t now = status_time();
    snprintf(clock,capacity,"--:--");
    if (now > 1704067200) {
        time_t china = now + 8 * 3600;
        struct tm date;
        if (gmtime_r(&china, &date)) snprintf(clock, capacity, "%02d:%02d", date.tm_hour, date.tm_min);
    }
}
void ui_nav_clock_draw(uint8_t *fb) {
    char clock[12]; clock_text(clock,sizeof(clock));
    ui_text_fixed_vc(fb,36,40,UI_STATUS_TIME_PX,clock,EPD_DRAW_ALIGN_LEFT,false);
}
void ui_nav_status(uint8_t *fb) {
    status_prepare_metrics();
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    char clock[12]; clock_text(clock,sizeof(clock));
    const int status_center_y = 40;
    // 固定状态栏字号，让系统字体缩放时文字仍与图标共用中心线。
    // Keep status type fixed so text and glyphs share one centerline at every system font scale.
    ui_text_fixed_vc(fb, 36, status_center_y, UI_STATUS_TIME_PX, clock, EPD_DRAW_ALIGN_LEFT, false);
    read_pico_transfer_status_t network = {0};
    read_pico_transfer_get_status(&network);
    const bool wifi_visible = network.network_ready && network.mode == READ_PICO_TRANSFER_MODE_STA;
    const bool bluetooth_visible = app_settings_ble_turner();
    char percent[8] = "--%";
    int gauge_percent = pmu_battery_percent(pmu);
    if (gauge_percent >= 0) snprintf(percent, sizeof(percent), "%u%%", (unsigned)gauge_percent);
    // 按实际百分比宽度排列网络图标，留 10 像素间隔；墨色与电池相同。
    // Place the network marks 10 px before the measured percentage, using the battery ink.
    // 右侧集群锚点随屏幕宽度：竖屏(684)与原硬编码逐像素一致，横屏阅读时贴到右缘；
    // 百分比宽度按数字实测（r93 引入的比例字体实测宽度），右锚保持参数化。
    // Anchor the right cluster to the screen width: byte-identical to the old
    // constants in portrait (684) and flush with the right edge in landscape reading;
    // percent width is measured from the actual digits (r93), the right anchor stays parametric.
    const int status_right = ui_w() - 89;
    const int percent_left = status_right - status_number_width(percent);
    const int network_right = percent_left - 10;
    const int wifi_center = network_right - UI_NAV_NETWORK_ICON_PX / 2;
    const int bluetooth_center = wifi_center - (wifi_visible ? UI_NAV_NETWORK_ICON_PX + 8 : 0);
    int signature_width = 280;
    const int clock_right = 36 + status_number_width(clock);
    int available = 2 * (UI_LOCK_WIDTH / 2 - clock_right - 12);
    if (available < signature_width) signature_width = available > 0 ? available : 0;
    int group_left = percent_left;
    if (wifi_visible || bluetooth_visible) {
        group_left = (bluetooth_visible ? bluetooth_center : wifi_center) - UI_NAV_NETWORK_ICON_PX / 2;
    }
    available = 2 * (group_left - 12 - UI_LOCK_WIDTH / 2);
    if (available < signature_width) signature_width = available > 0 ? available : 0;
    char signature[96];
    snprintf(signature, sizeof(signature), "%s", app_settings_status_signature());
    status_fit_signature(signature, signature_width);
    if (signature[0]) ui_text_fixed_vc(fb, UI_LOCK_WIDTH / 2, status_center_y, UI_STATUS_SIGNATURE_PX,
                                      signature, EPD_DRAW_ALIGN_CENTER, false);
    if (wifi_visible) ui_nav_wifi_icon(fb, wifi_center, status_center_y, UI_NAV_NETWORK_ICON_PX, UI_GRAY_BLACK);
    if (bluetooth_visible)
        ui_draw_icon(fb, bluetooth_center, status_center_y, UI_NAV_NETWORK_ICON_PX,
                     UI_ICON_BLUETOOTH, UI_GRAY_BLACK);
    EpdRect battery = {status_right + 11, status_center_y - 11, 40, 22};
    ui_draw_round_rect(fb, battery, 6, UI_GRAY_BLACK);
    ui_fill_round_rect(fb, (EpdRect){status_right + 53, status_center_y - 4, 5, 8}, 2, UI_GRAY_BLACK);
    bool charging = pmu_battery_charging(pmu);
    if (gauge_percent >= 0) {
        int width = (31 * gauge_percent + 50) / 100;
        if (width > 0) ui_fill_round_rect(fb, (EpdRect){status_right + 15, status_center_y - 7, width, 14}, 4, UI_GRAY_BLACK);
    }
    if (charging) {
        // 闪电只落在深色电量块上：透明像素保留底色，白色墨在浅色区自然消失，
        // 不必再按电量高低切换墨色。
        // The bolt shows only over the dark fill: transparent pixels keep the background,
        // so white ink vanishes on the light part and no SOC-dependent colour is needed.
        ui_draw_icon(fb, 627, status_center_y, UI_NAV_BOLT_ICON_PX, UI_ICON_ZAP, UI_GRAY_WHITE);
    }
    ui_text_fixed_vc(fb, 595, status_center_y, UI_STATUS_PERCENT_PX, percent, EPD_DRAW_ALIGN_RIGHT, false);
}

int ui_nav_hit(uint16_t x, uint16_t y) {
    if (x >= UI_LOCK_WIDTH || y < UI_NAV_TOUCH_TOP || y >= UI_LOCK_HEIGHT) return -1;
    int tab = x * 4 / UI_LOCK_WIDTH;
    return tab < 4 ? tab : -1;
}

void ui_nav_request(app_ctx_t *ctx, int index) {
    if (index >= 0 && index < 4) ctx->request_app = app_at(index);
}
