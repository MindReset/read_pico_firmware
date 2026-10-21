/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 传书页负责两种接入模式生命周期、书源注入与状态展示；HTTP和文件接收属于独立组件。
 * Transfer page owns both network modes, storage injection and status; the component receives files.
 *
 * 冻结：按用户双模式要求提供热点与已有WiFi；离开传书时停服，配网后已连接的 STA 保持在线；深睡/断电会中断传输。
 * render只画快照；内置存储单文件上限由book_store提供，不格式化TF卡。
 * 上传并发扫描曾欠载，页内增加预填余量；离页停服后恢复，不改波形或像素时钟。
 * 用户要求设备端配网：停服后扫描选网，ASCII密码只在连接保存时写入，离开输入页即清空。
 * 用户要求输入光标：密码框常亮光标可轻点定位，显示/隐藏保留位置，离页一并清空。
 * 用户授权新输入法：WiFi 密码共用英文、大小写、数字及符号键盘，不启用中文候选。
 * 用户验收要求停止后恢复进入传书前的页面或菜单位置；遗忘网络必须确认。
 * 用户要求联网后提供网址二维码；热点用单码切换连接/网页，网络变更清除旧码。
 * 卡失效时停止并汇合接收任务，清除旧容量与二维码；当前请求不得切换存储源。
 * 配网列表与确认页底栏可切换主页面；密码页底部保留输入操作键，已连接的 STA 在离页后保活。
 * 正在上传时禁止主动切页；USB 退出必须先断开电脑再启动本机重新挂载。
 * Frozen: AP/STA transfer stops on exit while a provisioned connected STA stays online; deep sleep or power loss interrupts transfer.
 * Render only paints snapshots; book_store defines the flash file limit. Never format the TF card.
 * Concurrent uploads underrun scan queues; increase prefill until service exit without changing waveforms or pixel clocks.
 * User-requested device provisioning scans while stopped; save ASCII passwords only on connect and clear input on leaving the editor.
 * User-requested caret: tap to position a steady password caret; show/hide preserves position, and exit clears it too.
 * Authorized keyboard revision: WiFi passwords share English, case, number and symbol keys without Chinese candidates.
 * 用户修订：输入密码只刷新输入框与字数，布局切换才重画键盘；不在打字时全刷。
 * User revision: password typing refreshes its field/count; repaint keys only on layout switches, with no typing-triggered wipes.
 * Acceptance requires returning to the page or menu position used to enter transfer; forgetting WiFi requires confirmation.
 * User-requested URL QR follows network readiness; AP switches one code between joining and browsing, discarding stale codes on changes.
 * Lost media stops and joins reception and clears capacity/QR; never switch storage beneath an active request.
 * Provisioning lists and confirmation keep the main tabs active; the password editor keeps its input actions, and connected STA persists after exit.
 * Navigation waits for an active upload; USB exit disconnects the host before local remount.
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "app.h"
#include "app_content_open.h"
#include "book_store.h"
#include "book_progress.h"
#include "book_title.h"
#include "dict_lookup.h"
#include "dict_popup.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "read_pico_transfer.h"
#include "read_pico_sd.h"
#include "push_sink.h"
#include "read_pico_pmu.h"
#include "settings.h"
#include "ttf_font.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_text_input.h"
#include "ui_keyboard.h"
#include "ui_menu.h"
#include "ui_nav.h"
#include "ui_wifi_qr.h"
#include "usb_storage.h"

static const char* TAG = "transfer_page";
static book_store_root_t s_root;
static read_pico_transfer_status_t s_status;
static bool s_start_pending, s_settle, s_any_changed;
static bool s_media_lost;
static int s_pressed = -1;
static read_pico_transfer_mode_t s_mode = READ_PICO_TRANSFER_MODE_AP;
static bool s_session_started;
static int64_t s_poll_ms;
static uint64_t s_free;
static size_t s_heap_before, s_internal_before;
static EpdRect s_area;
static bool s_qr_url, s_qr_ready;
static char s_usb_message[96];
static bool s_usb_requested, s_wifi_setup_requested, s_wifi_settings_only;
static bool s_wifi_upload_requested, s_hotspot_requested, s_direct_entry, s_usb_entry;
// 手机 BLE 推流（二期 M1）：s_push_requested 由入口置位，on_enter 据此武装；
// s_push_poll_ms 节流武装页刷新，s_push_note 是页内动态消息（错误/已接收占位）。
// Phone BLE push (phase 2 M1): s_push_requested is set by the entry and on_enter
// arms on it; s_push_poll_ms throttles the arm-page refresh and s_push_note is
// the page's live message (errors / received placeholders).
static bool s_push_requested;
static int64_t s_push_poll_ms;
static char s_push_note[96];
// Step 4：单词卡开关与活区比对簿记。/ Step 4: the word-card switch and the
// live-region comparison bookkeeping.
static bool s_push_word_card;
static push_state_t s_push_last_state = PUSH_STATE_IDLE;
static uint32_t s_push_last_left_s = 1; // 初值取不到的值，首拍必重绘 / an impossible first value so the first tick repaints
static uint32_t s_push_last_recv = 1, s_push_last_total = 1;
static char s_push_last_adv[64];
static bool s_usb_start_pending;
static int64_t s_usb_retry_ms;
static bool s_usb_host_connected;
static uint64_t s_usb_capacity_bytes;

static uint32_t s_signature_revision, s_signature_seen;
static bool transfer_signature_get(char *out, size_t cap) {
    const char *text = app_settings_status_signature();
    if (!out || !cap || strlen(text) >= cap) return false;
    snprintf(out, cap, "%s", text); return true;
}
static esp_err_t transfer_signature_set(const char *text) {
    app_settings_set_status_signature(text);
    if (strcmp(app_settings_status_signature(), text)) return ESP_FAIL;
    __atomic_add_fetch(&s_signature_revision, 1, __ATOMIC_RELEASE);
    return ESP_OK;
}

static bool transfer_book_file(const char *path) {
    const char *ext = strrchr(path, '.');
    return ext && (!strcasecmp(ext, ".epub") || !strcasecmp(ext, ".txt"));
}

static bool transfer_path_below(const char *path, const char *directory) {
    size_t n = strlen(directory);
    return !strncmp(path, directory, n) && (!path[n] || path[n] == '/');
}

static void transfer_directory_deleted(const char *path) {
    if (transfer_path_below(app_settings_books_dir(), path))
        (void)app_settings_set_books_dir("/sdcard/books");
    if (transfer_path_below(app_settings_fonts_dir(), path))
        (void)app_settings_set_fonts_dir("/sdcard/fonts");
}

static void transfer_directory_moved(const char *old_path, const char *new_path) {
    const char *selected[2] = {app_settings_books_dir(), app_settings_fonts_dir()};
    for (int i = 0; i < 2; ++i) {
        if (!transfer_path_below(selected[i], old_path)) continue;
        char updated[288];
        if (snprintf(updated, sizeof(updated), "%s%s", new_path, selected[i] + strlen(old_path)) >= (int)sizeof(updated)) continue;
        bool saved = i == 0 ? app_settings_set_books_dir(updated) : app_settings_set_fonts_dir(updated);
        if (!saved) {
            if (i == 0) (void)app_settings_set_books_dir("/sdcard/books");
            else (void)app_settings_set_fonts_dir("/sdcard/fonts");
        }
    }
}

static void transfer_file_deleted(const char *path) {
    if (transfer_book_file(path)) {
        (void)book_progress_forget(path);
        (void)book_title_clear(path);
    }
    if (!strcmp(app_settings_system_font_path(), path)) app_settings_set_system_font_path("");
    if (!strcmp(app_settings_font_path(), path)) app_settings_set_font_path("");
    if (!strcmp(app_settings_wallpaper_path(), path)) app_settings_set_wallpaper_path("");
}

static void transfer_file_moved(const char *old_path, const char *new_path, uint32_t size) {
    if (transfer_book_file(old_path)) {
        book_progress_t progress = {0};
        bool saved = book_progress_load(old_path, size, &progress);
        char last[BOOK_STORE_PATH_MAX] = {0};
        bool was_last = book_progress_last_path(last, sizeof(last)) && !strcmp(last, old_path);
        if (saved) (void)book_progress_save(new_path, &progress);
        (void)book_progress_forget(old_path);
        if (was_last) (void)book_progress_set_last_path(new_path);
    }
    if (transfer_book_file(old_path)) (void)book_title_clear(old_path);
    if (!strcmp(app_settings_system_font_path(), old_path)) app_settings_set_system_font_path(new_path);
    if (!strcmp(app_settings_font_path(), old_path)) app_settings_set_font_path(new_path);
    if (!strcmp(app_settings_wallpaper_path(), old_path)) app_settings_set_wallpaper_path(new_path);
}

static bool transfer_set_wallpaper(const char *path) {
    if (strncmp(path, "/sdcard/pictures/", 17)) return false;
    app_settings_set_wallpaper_path(path);
    if (strcmp(app_settings_wallpaper_path(), path)) return false;
    app_settings_set_lock_style(1);
    return app_settings_lock_style() == 1;
}

void app_transfer_request_method_picker(void) {
    s_usb_requested = s_hotspot_requested = s_wifi_upload_requested = s_wifi_setup_requested = false;
    s_wifi_settings_only = false;
}

void app_transfer_request_usb_start(void) {
    s_usb_requested = true;
}

void app_transfer_request_wifi_setup(void) {
    s_wifi_setup_requested = true;
    s_wifi_settings_only = true;
}

void app_transfer_request_wifi_upload(void) {
    s_wifi_upload_requested = true;
    s_wifi_settings_only = false;
}

void app_transfer_request_hotspot_start(void) {
    s_hotspot_requested = true;
    s_wifi_settings_only = false;
}

// 手机推送入口（二期 M1）：进页即武装。其它入口只置标志，武装动作在 on_enter，
// 与 WiFi/热点入口同款——页面没真正显示之前不碰射频。
// The phone-push entry (phase 2 M1): entering the page arms it. Like the WiFi and
// hotspot entries it only sets a flag; the arming happens in on_enter, so nothing
// touches the radio before the page is actually on screen.
void app_transfer_request_push(void) {
    s_push_requested = true;
}

static void clear_qr(void) {
    ui_wifi_qr_clear();
    s_qr_ready = false;
}

// 仅在网络快照变化或用户切换时编码，render与上传进度不触发编码。
// Encode only on network snapshot changes or user switching, never from render or upload progress.
static void prepare_qr(void) {
    clear_qr();
    if (!s_status.network_ready) return;
    s_qr_ready = s_qr_url || s_mode == READ_PICO_TRANSFER_MODE_STA
        ? ui_wifi_qr_prepare_url(s_status.url)
        : ui_wifi_qr_prepare(s_status.ssid, READ_PICO_TRANSFER_PASSWORD);
}

typedef enum { TRANSFER_METHODS, TRANSFER_HOME, TRANSFER_NETWORKS, TRANSFER_PASSWORD, TRANSFER_PUSH } transfer_view_t;
#define NETWORK_ROWS 4
#define PASSWORD_MAX 64
static transfer_view_t s_view;
// gen18（18-a）：本次离页若是 OPEN_BOOK(push.txt) 的交接就不收摊——sink 转后台
// 属主，手机在宽限期/窗口内还能直接连推。用户手动离开照旧收摊。
// gen18 (18-a): if this exit is the OPEN_BOOK(push.txt) handoff, do not tear
// down - the sink moves to a background owner and the phone can still push
// within the grace/window. Manual exits close up as before.
static bool s_push_handed_off;
static read_pico_transfer_network_t s_networks[READ_PICO_TRANSFER_SCAN_MAX], s_selected;
static size_t s_network_count, s_network_page;
static bool s_scan_pending, s_network_connect_pending, s_saved_configured, s_password_visible, s_forget_confirm;
static char s_saved_ssid[33], s_password[PASSWORD_MAX + 1], s_network_message[96], s_method_message[96];
static ui_text_edit_t s_password_input;

static esp_err_t s_scan_error;

static void render(app_ctx_t* ctx, uint8_t* fb);
static void stop_session(void);
static bool stop_upload_if_idle(void);

static void transfer_header(uint8_t *fb, const char *title, const char *subtitle) {
    ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, 342, 107, 34, title, EPD_DRAW_ALIGN_CENTER, false);
    if (subtitle && subtitle[0]) ui_text(fb, 36, 153, 20, subtitle, EPD_DRAW_ALIGN_LEFT, false);
    if (subtitle && subtitle[0]) ui_hairline(fb, 190, 36, 612, UI_GRAY_LIGHT);
}

static EpdRect method_control_rect(int id) {
    return (EpdRect){36, 224 + id * 112, 612, 112};
}

// S05：入口只负责选择传输方式，具体服务信息留给下一页。
// S05 keeps method selection separate from the service details shown on the next page.
static void draw_methods(uint8_t *fb) {
    ui_clear_page(fb);
    ui_fill_rect_fast((EpdRect){0, 0, UI_LOCK_WIDTH, UI_NAV_TOP}, 0xe0, fb);
    transfer_header(fb, "Wifi/热点传书", NULL);
    ui_text(fb, 42, 180, 21, "选择方式", EPD_DRAW_ALIGN_LEFT, false);
    EpdRect card = {36, 224, 612, 336};
    ui_fill_round_rect(fb, card, 24, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, card, 24, 0x60);
    ui_draw_round_rect(fb, (EpdRect){37, 225, 610, 334}, 23, 0x60);
    static const char *titles[] = {"WiFi传书", "热点传书", "手机推送"};
    static const char *details[] = {
        "与手机或电脑连接同一个无线网络",
        "由墨水屏创建临时网络，离线也能传书",
        "安卓复制文字，通过蓝牙发送到墨水屏"
    };
    for (int i = 0; i < 3; ++i) {
        EpdRect row = method_control_rect(i);
        if (s_pressed == i) ui_draw_pressed_round_rect(fb, row, 0);
        if (i) ui_fill_rect_fast((EpdRect){68, row.y, 548, 2}, 0x80, fb);
        int cy = row.y + row.height / 2;
        if (i == 0) ui_nav_wifi_icon(fb, 72, cy, 30, 0x6a);
        else if (i == 1) ui_draw_icon(fb, 72, cy, 48, UI_ICON_RADIO_TOWER, 0x6a);
        else ui_draw_icon(fb, 72, cy, 40, UI_ICON_BLUETOOTH, 0x6a);
        ui_text(fb, 112, row.y + 21, 27, titles[i], EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 112, row.y + 64, 19, details[i], EPD_DRAW_ALIGN_LEFT, false);
        ui_text_vc(fb, 617, cy, 29, "›", EPD_DRAW_ALIGN_CENTER, false);
    }
    if (s_method_message[0]) {
        ui_fill_round_rect(fb, (EpdRect){36, 584, 612, 76}, 20, 0xd8);
        ui_text_vc(fb, 342, 622, 20, s_method_message, EPD_DRAW_ALIGN_CENTER, false);
    }
    ui_text(fb, 42, 694, 19, "浏览器传书支持 EPUB、TXT、字体和图片文件。", EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 42, 734, 18, "传输完成后，新内容会自动出现在书架或字体列表。", EPD_DRAW_ALIGN_LEFT, false);
    ui_nav_draw(fb, 2);
}

/* ---- 设备端配网 / Device provisioning ---- */
static void fit_label(char* text, int px, int width) {
    while (*text && ttf_text_width_px(px, text) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}

static void clear_password(void) {
    volatile char* p = s_password;
    for (size_t i = 0; i < sizeof(s_password); ++i) p[i] = 0;
    ui_text_edit_init(&s_password_input, s_password, sizeof(s_password));
    s_password_visible = false;
    ui_keyboard_end();
}

static EpdRect network_control_rect(int id) {
    if (id >= 0 && id < NETWORK_ROWS) return (EpdRect){36, 465 + id * 95, 612, 95};
    if (id == 6) return (EpdRect){501, 244, 123, 56};
    if (id == 11) return s_forget_confirm ? ui_row_rect(0, 2, 600, UI_BTN_H) : (EpdRect){501, 315, 123, 52};
    if (id == 12) return ui_row_rect(1, 2, 600, UI_BTN_H);
    bool paged = s_network_count > NETWORK_ROWS;
    if (id == 7) return paged ? (EpdRect){36, 873, 64, 66} : (EpdRect){0};
    if (id == 8) return paged ? (EpdRect){112, 873, 460, 66} : (EpdRect){36, 873, 612, 66};
    if (id == 9) return paged ? (EpdRect){584, 873, 64, 66} : (EpdRect){0};
    return (EpdRect){0};
}

static EpdRect password_control_rect(int id) {
    if (id == 44) return (EpdRect){36, 434, 278, 68};
    if (id == 45) return (EpdRect){500, 215, 148, 58};
    if (id == 46) return (EpdRect){330, 434, 318, 68};
    if (id == 47) return (EpdRect){UI_MARGIN, 288, ui_content_width(), 86};
    return (EpdRect){0};
}

static void provisioning_button(uint8_t* fb, EpdRect rect, const char* text, int id) {
    if (s_pressed == id) ui_draw_pressed_round_rect(fb, rect, UI_BTN_RADIUS);
    ui_draw_button(fb, rect, text, false);
}

static void network_border(uint8_t *fb, EpdRect rect, int radius) {
    ui_draw_round_rect(fb, rect, radius, 0x50);
    ui_draw_round_rect(fb, (EpdRect){rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2}, radius - 1, 0x50);
}

static void network_button(uint8_t *fb, int id, const char *label, bool dark) {
    EpdRect rect = network_control_rect(id);
    ui_fill_round_rect(fb, rect, 16, s_pressed == id ? 0xb0 : dark ? UI_GRAY_BLACK : UI_GRAY_WHITE);
    if (!dark) network_border(fb, rect, 16);
    ui_text_vc(fb, rect.x + rect.width / 2, rect.y + rect.height / 2, 24, label,
               EPD_DRAW_ALIGN_CENTER, dark && s_pressed != id);
}

static void draw_networks(uint8_t* fb) {
    ui_clear_page(fb);
    ui_fill_rect_fast((EpdRect){0, 0, UI_LOCK_WIDTH, UI_NAV_TOP}, 0xe0, fb);
    if (s_forget_confirm) {
        transfer_header(fb, "遗忘网络", "仅清除 WiFi 连接信息，不影响图书");
        char name[33];
        snprintf(name, sizeof(name), "%s", s_saved_ssid);
        fit_label(name, UI_PX_BODY, ui_content_width());
        ui_text(fb, UI_MARGIN, 350, UI_PX_BODY, name, EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, UI_MARGIN, 450, UI_PX_CAPTION, s_network_message, EPD_DRAW_ALIGN_LEFT, false);
        provisioning_button(fb, network_control_rect(11), "确认遗忘", 11);
        provisioning_button(fb, network_control_rect(12), "取消", 12);
        ui_nav_draw(fb, 3);
        return;
    }
    transfer_header(fb, "无线网络", NULL);
    ui_text(fb, 42, 182, 24, "当前网络", EPD_DRAW_ALIGN_LEFT, false);
    EpdRect current = {36, 224, 612, 154};
    ui_fill_round_rect(fb, current, 22, UI_GRAY_WHITE);
    network_border(fb, current, 22);
    if (s_saved_configured) {
        char saved[40]; snprintf(saved, sizeof(saved), "%s", s_saved_ssid); fit_label(saved, 29, 375);
        ui_nav_wifi_icon(fb, 74, 265, 28, UI_GRAY_BLACK);
        ui_text(fb, 104, 243, 29, saved, EPD_DRAW_ALIGN_LEFT, false);
        const char *state = s_status.network_ready ? "已连接" :
            s_network_connect_pending || s_status.state == READ_PICO_TRANSFER_STARTING ? "正在连接…" : "已保存 · 尚未连接";
        ui_text(fb, 64, 297, 23, state, EPD_DRAW_ALIGN_LEFT, false);
        char address[80];
        const char *url = !strncmp(s_status.url, "http://", 7) ? s_status.url + 7 : s_status.url;
        snprintf(address, sizeof(address), "%.79s", s_status.network_ready ? url[0] ? url : "已连接，可传书和对时" : "连接后可传书和对时");
        fit_label(address, 21, 419);
        ui_text(fb, 64, 336, 21, address, EPD_DRAW_ALIGN_LEFT, false);
        network_button(fb, 6, s_status.network_ready ? "断开" : "连接", !s_status.network_ready);
        if (s_pressed == 11) ui_draw_pressed_round_rect(fb, network_control_rect(11), 14);
        ui_text_vc(fb, 563, 341, 21, "遗忘网络", EPD_DRAW_ALIGN_CENTER, false);
    } else {
        ui_text(fb, 64, 253, 29, "尚未保存网络", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 64, 312, 23, "请从下方列表选择 2.4 GHz WiFi", EPD_DRAW_ALIGN_LEFT, false);
    }
    char count[64];
    snprintf(count, sizeof(count), "%u 个 · %u/%u 页", (unsigned)s_network_count,
             (unsigned)s_network_page + 1, (unsigned)(s_network_count ? (s_network_count + NETWORK_ROWS - 1) / NETWORK_ROWS : 1));
    ui_text(fb, 42, 418, 24, "附近网络", EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 642, 420, 21, count, EPD_DRAW_ALIGN_RIGHT, false);
    EpdRect list = {36, 465, 612, 380};
    ui_fill_round_rect(fb, list, 22, UI_GRAY_WHITE);
    if (s_scan_pending) {
        ui_text_vc(fb, 342, 655, 25, "正在扫描 2.4 GHz WiFi", EPD_DRAW_ALIGN_CENTER, false);
    } else if (!s_network_count) {
        ui_text_vc(fb, 342, 620, 27, s_scan_error == ESP_OK ? "没有发现可用网络" : "扫描没有完成",
                   EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 678, 22, s_scan_error == ESP_OK ? "请靠近路由器后重新扫描" : "点下方按钮自动重试",
                   EPD_DRAW_ALIGN_CENTER, false);
    }
    for (int row = 0; !s_scan_pending && row < NETWORK_ROWS; ++row) {
        size_t index = s_network_page * NETWORK_ROWS + row;
        if (index >= s_network_count) break;
        const read_pico_transfer_network_t* network = &s_networks[index];
        EpdRect rect = network_control_rect(row);
        if (s_pressed == row) ui_draw_pressed_round_rect(fb, rect, 0);
        if (row) ui_fill_rect_fast((EpdRect){64, rect.y, 556, 2}, 0x70, fb);
        char name[33], detail[64];
        snprintf(name, sizeof(name), "%s", network->ssid);
        fit_label(name, 28, 520);
        ui_text(fb, 64, rect.y + 16, 28, name, EPD_DRAW_ALIGN_LEFT, false);
        snprintf(detail, sizeof(detail), "%s · %s", network->rssi > -60 ? "信号强" : network->rssi > -75 ? "信号良好" : "信号一般",
                 !network->supported ? "暂不支持" : network->requires_password ? "需要密码" : "开放网络");
        ui_text(fb, 64, rect.y + 59, 21, detail, EPD_DRAW_ALIGN_LEFT, false);
        ui_text_vc(fb, 615, rect.y + 47, 27, "›", EPD_DRAW_ALIGN_CENTER, false);
    }
    network_border(fb, list, 22);
    if (s_network_count > NETWORK_ROWS) {
        network_button(fb, 7, "‹", false);
        network_button(fb, 9, "›", false);
    }
    network_button(fb, 8, s_scan_error == ESP_OK ? "重新扫描" : "重试扫描", false);
    if (s_network_count <= NETWORK_ROWS) ui_draw_icon(fb, 597, 906, 32, UI_ICON_ROTATE_CW, UI_GRAY_BLACK);
    ui_text(fb, 42, 976, 21, "支持 2.4 GHz WiFi（信道 1–13）", EPD_DRAW_ALIGN_LEFT, false);
    char message[96];
    snprintf(message, sizeof(message), "%s", s_network_message[0] ? s_network_message :
             s_status.network_ready ? "退出此页后，WiFi 仍保持连接。" : "连接后，可退出此页继续使用。");
    fit_label(message, 21, 600);
    ui_text(fb, 42, 1014, 21, message, EPD_DRAW_ALIGN_LEFT, false);
    ui_nav_draw(fb, 3);
}

static void draw_password(uint8_t* fb) {
    ui_clear_page(fb);
    transfer_header(fb, "连接 WiFi", "输入完成后连接，密码只保存在本机");
    char name[33];
    snprintf(name, sizeof(name), "%s", s_selected.ssid);
    fit_label(name, UI_PX_BODY, ui_content_width());
    ui_text(fb, UI_MARGIN, 176, UI_PX_BODY, name, EPD_DRAW_ALIGN_LEFT, false);
    char caption[80];
    size_t len = strlen(s_password);
    snprintf(caption, sizeof(caption), "%s · %u/%u", s_selected.requires_password ? "密码" : "开放网络，无需密码", (unsigned)len, PASSWORD_MAX);
    ui_text_fixed_vc(fb, UI_MARGIN, 240, 22, caption, EPD_DRAW_ALIGN_LEFT, false);
    ui_text_input_draw(fb, &s_password_input, password_control_rect(47), UI_PX_BODY,
                       !s_password_visible, NULL);
    ui_text_fixed_vc(fb, 36, 399, 22, s_network_message[0] ? s_network_message : "支持英文、数字、符号和空格", EPD_DRAW_ALIGN_LEFT, false);
    provisioning_button(fb, password_control_rect(44), "取消", 44);
    provisioning_button(fb, password_control_rect(45), s_password_visible ? "隐藏" : "显示", 45);
    provisioning_button(fb, password_control_rect(46), "连接并保存", 46);
    ui_keyboard_draw(fb, 560);
}

static void queue_network_start(read_pico_transfer_mode_t mode) {
    s_mode = mode;
    s_qr_url = mode == READ_PICO_TRANSFER_MODE_STA;
    clear_qr();
    s_view = s_wifi_settings_only ? TRANSFER_NETWORKS : TRANSFER_HOME;
    s_forget_confirm = false;
    s_scan_pending = false;
    s_pressed = -1;
    clear_password();
    memset(&s_status, 0, sizeof(s_status));
    s_status.mode = s_mode;
    read_pico_transfer_get_saved_wifi(s_status.wifi_ssid, &s_status.wifi_configured);
    s_saved_configured = s_status.wifi_configured;
    snprintf(s_saved_ssid, sizeof(s_saved_ssid), "%s", s_status.wifi_ssid);
    if (s_wifi_settings_only) {
        s_network_connect_pending = true;
        snprintf(s_network_message, sizeof(s_network_message), "正在连接 %s…", s_status.wifi_ssid);
    } else s_start_pending = true;
}

static bool enter_networks(void) {
    if (!read_pico_transfer_try_stop_if_idle()) return false;
    stop_session();
    s_start_pending = false;
    s_view = TRANSFER_NETWORKS;
    s_forget_confirm = false;
    s_network_count = s_network_page = 0;
    s_scan_pending = true;
    s_scan_error = ESP_OK;
    s_network_message[0] = 0;
    s_saved_ssid[0] = 0;
    s_saved_configured = false;
    read_pico_transfer_get_saved_wifi(s_saved_ssid, &s_saved_configured);
    clear_password();
    return true;
}

static bool start_hotspot_service(void) {
    if (!read_pico_transfer_try_stop_if_idle()) return false;
    stop_session();
    s_mode = READ_PICO_TRANSFER_MODE_AP;
    s_qr_url = false;
    clear_qr();
    memset(&s_status, 0, sizeof(s_status));
    s_status.mode = s_mode;
    s_start_pending = true;
    s_view = TRANSFER_HOME;
    s_method_message[0] = 0;
    return true;
}

// 提示页先呈现，下一轮才同步扫描；输入页不轮询已停服的传书状态。
// Present the scan notice first, scan synchronously next tick, and do not poll stopped transfer state in the editor.
static app_redraw_t network_ui_tick(app_ctx_t* ctx) {
    if (s_view != TRANSFER_NETWORKS || s_forget_confirm || ctx->consumed) return APP_REDRAW_NONE;
    if (s_network_connect_pending) {
        s_network_connect_pending = false;
        read_pico_transfer_cfg_t cfg = {.mode = READ_PICO_TRANSFER_MODE_STA, .network_only = true};
        esp_err_t err = read_pico_transfer_start(&cfg);
        s_session_started = err == ESP_OK;
        read_pico_transfer_get_status(&s_status);
        if (err != ESP_OK) snprintf(s_network_message, sizeof(s_network_message),
                                    read_pico_transfer_rf_blocked()
                                        ? "飞行模式已开启，请先在设置 › 无线连接中关闭"
                                        : "连接启动失败，请重试");
        return APP_REDRAW_PAGE;
    }
    if (s_session_started) {
        read_pico_transfer_service_poll();
        read_pico_transfer_status_t next = {0};
        read_pico_transfer_get_status(&next);
        bool changed = next.state != s_status.state || next.network_ready != s_status.network_ready ||
                       next.last_error != s_status.last_error;
        s_status = next;
        if (s_status.network_ready) snprintf(s_network_message, sizeof(s_network_message), "连接成功");
        else if (s_status.state == READ_PICO_TRANSFER_ERROR) snprintf(s_network_message, sizeof(s_network_message), "连接失败，请检查密码或信号");
        return changed ? APP_REDRAW_PAGE : APP_REDRAW_NONE;
    }
    if (!s_scan_pending) return APP_REDRAW_NONE;
    s_scan_pending = false;
    s_network_count = s_network_page = 0;
    // The scan notice is already in the framebuffer. Drop rebuildable glyph
    // bitmaps before starting WiFi, then recreate them while drawing the result.
    // This prevents the hosted radio from competing with the UI cache at init.
    ttf_font_cache_clear();
    esp_err_t err = read_pico_transfer_scan_wifi(s_networks, &s_network_count);
    s_scan_error = err;
    if (err != ESP_OK) {
        s_network_count = 0;
        // 飞行模式下扫描被闸门挡住，此时「扫描失败」是误导——列表根本扫不出来，
        // 所以要先说清楚原因，让用户知道该去关飞行模式而不是反复重扫。
        // In flight mode the scan is gated off, so "scan failed" would be misleading: there is no
        // list to retry into. Say why, so the user turns flight mode off instead of rescanning.
        snprintf(s_network_message, sizeof(s_network_message), "%s",
                 read_pico_transfer_rf_blocked() ? "飞行模式已开启，请先在设置 › 无线连接中关闭"
                 : err == ESP_ERR_NO_MEM ? "内存已清理，请点下方按钮重试" : "扫描失败，请点下方按钮重试");
    }
    else if (!s_network_count) snprintf(s_network_message, sizeof(s_network_message), "未发现网络，请靠近路由器后重扫");
    else s_network_message[0] = 0;
    s_pressed = -1;
    return APP_REDRAW_PAGE;
}

static int provisioning_hit(uint16_t x, uint16_t y) {
    if (s_forget_confirm) {
        for (int i = 11; i <= 12; ++i) if (ui_rect_hit(network_control_rect(i), x, y)) return i;
        return -1;
    }
    if (s_view == TRANSFER_PASSWORD) {
        for (int i = 44; i < 48; ++i) if (ui_rect_hit(password_control_rect(i), x, y)) return i;
    } else {
        for (int i = 0; i < 12; ++i) {
            if (i < NETWORK_ROWS && (s_scan_pending || s_network_page * NETWORK_ROWS + i >= s_network_count)) continue;
            if ((i == 6 || i == 11) && !s_saved_configured) continue;
            if (ui_rect_hit(network_control_rect(i), x, y)) return i;
        }
    }
    return -1;
}

static app_redraw_t provisioning_action(int id) {
    if (s_forget_confirm) {
        if (id == 12) { s_forget_confirm = false; s_network_message[0] = 0; }
        else if (id == 11) {
            // 断联后页面标记可能已丢失；以组件的真实会话为准，上传中不能清配置。
            // A failed reconnect may lose the page flag; stop the actual component session, never an active upload.
            if (!stop_upload_if_idle()) {
                snprintf(s_network_message, sizeof(s_network_message), "正在传输，请完成后再遗忘");
                return APP_REDRAW_PAGE;
            }
            s_network_connect_pending = s_scan_pending = false;
            esp_err_t err = read_pico_transfer_forget_wifi();
            if (err == ESP_OK) {
                read_pico_transfer_get_status(&s_status);
                s_forget_confirm = false;
                s_saved_configured = false;
                s_saved_ssid[0] = 0;
                snprintf(s_network_message, sizeof(s_network_message), "已遗忘网络");
            } else snprintf(s_network_message, sizeof(s_network_message), "遗忘失败，请重试或取消");
        }
        return APP_REDRAW_PAGE;
    }
    if (s_view == TRANSFER_NETWORKS) {
        if (id < NETWORK_ROWS) {
            s_selected = s_networks[s_network_page * NETWORK_ROWS + id];
            if (!s_selected.supported) {
                snprintf(s_network_message, sizeof(s_network_message), "此网络认证暂不支持，请选择其他网络");
                return APP_REDRAW_PAGE;
            }
            if (!s_selected.requires_password) {
                esp_err_t err = read_pico_transfer_save_wifi(s_selected.ssid, "");
                if (err == ESP_OK) {
                    queue_network_start(READ_PICO_TRANSFER_MODE_STA);
                    return APP_REDRAW_PAGE;
                }
                snprintf(s_network_message, sizeof(s_network_message), "开放网络保存失败，请重试");
                return APP_REDRAW_PAGE;
            }
            clear_password();
            s_network_message[0] = 0;
            ui_keyboard_begin(&s_password_input, true);
            s_view = TRANSFER_PASSWORD;
        } else if (id == 6) {
            if (s_status.network_ready) {
                if (!stop_upload_if_idle()) {
                    snprintf(s_network_message, sizeof(s_network_message), "正在传输，请完成后再断开");
                    return APP_REDRAW_PAGE;
                }
                s_network_connect_pending = false;
                snprintf(s_network_message, sizeof(s_network_message), "已断开，连接信息保留");
            } else {
                // 未连上不等于驱动已停止；重试前先释放仍在等待或超时的旧 STA。
                // Disconnected does not mean stopped; release the pending or timed-out STA before retrying.
                if (!stop_upload_if_idle()) {
                    snprintf(s_network_message, sizeof(s_network_message), "正在传输，请完成后再连接");
                    return APP_REDRAW_PAGE;
                }
                s_network_connect_pending = false;
                queue_network_start(READ_PICO_TRANSFER_MODE_STA);
            }
        }
        else if (id == 7 && s_network_page) --s_network_page;
        else if (id == 8) {
            if (s_session_started) stop_session();
            s_scan_pending = true; s_scan_error = ESP_OK; s_network_message[0] = 0;
        }
        else if (id == 9 && (s_network_page + 1) * NETWORK_ROWS < s_network_count) ++s_network_page;
        else if (id == 10) queue_network_start(s_mode);
        else if (id == 11) { s_forget_confirm = true; s_network_message[0] = 0; }
        return APP_REDRAW_PAGE;
    }
    size_t len = strlen(s_password);
    if (id == 44) { clear_password(); s_network_message[0] = 0; s_view = TRANSFER_NETWORKS; return APP_REDRAW_PAGE; }
    else if (id == 45) s_password_visible = !s_password_visible;
    else if (id == 46) {
        if (s_selected.requires_password && len < 8) {
            snprintf(s_network_message, sizeof(s_network_message), "加密网络密码至少 8 个字符");
        } else {
            esp_err_t err = read_pico_transfer_save_wifi(s_selected.ssid,
                s_selected.requires_password ? s_password : "");
            if (err == ESP_OK) { queue_network_start(READ_PICO_TRANSFER_MODE_STA); return APP_REDRAW_PAGE; }
            snprintf(s_network_message, sizeof(s_network_message), "%s", err == ESP_ERR_INVALID_ARG ?
                     "密码须 8–63 字符或 64 位十六进制" : "保存失败，输入已保留，请重试");
        }
    }
    return APP_REDRAW_AREA;
}

static bool s_input_layout, s_input_settle;
static app_redraw_t password_paint(app_ctx_t *ctx, bool field, bool controls) {
    ui_keyboard_update_t update = ui_keyboard_update(ctx->fb, 560, password_control_rect(47),
        UI_PX_BODY, !s_password_visible, NULL, field);
    EpdRect extra = {UI_MARGIN, 215, ui_content_width(), controls ? 287 : 159};
    if (update.field || controls) {
        ui_clear_rect_fast(ctx->fb, (EpdRect){UI_MARGIN, 215, ui_content_width() - 156, 58});
        char caption[80];
        snprintf(caption, sizeof(caption), "%s · %u/%u", s_selected.requires_password ? "密码" : "开放网络，无需密码", (unsigned)strlen(s_password), PASSWORD_MAX);
        ui_text_fixed_vc(ctx->fb, UI_MARGIN, 240, 22, caption, EPD_DRAW_ALIGN_LEFT, false);
    }
    if (controls) {
        ui_clear_rect_fast(ctx->fb, (EpdRect){36, 377, 612, 36});
        ui_text_fixed_vc(ctx->fb, 36, 399, 22, s_network_message[0] ? s_network_message : "支持英文、数字、符号和空格", EPD_DRAW_ALIGN_LEFT, false);
        for (int i = 44; i <= 46; ++i) {
            EpdRect button = password_control_rect(i); ui_clear_rect_fast(ctx->fb, button);
            provisioning_button(ctx->fb, button, i == 44 ? "取消" : i == 45 ? s_password_visible ? "隐藏" : "显示" : "连接并保存", i);
        }
    }
    if (update.field || controls) update.area = update.area.width ? ui_rect_union(update.area, extra) : extra;
    s_area = update.area; s_input_layout = update.layout; s_input_settle = update.settle; s_settle = false;
    return update.area.width ? APP_REDRAW_AREA : APP_REDRAW_NONE;
}
static app_redraw_t provisioning_gesture(app_ctx_t* ctx, const ui_gesture_event_t* ev) {
    if (s_view == TRANSFER_PASSWORD) {
        if (ev->type == UI_GESTURE_PRESS)
            return ui_keyboard_press(ev->x0, ev->y0, 560, ctx->now_ms) ? password_paint(ctx, false, false) : APP_REDRAW_NONE;
        bool feedback = ev->type != UI_GESTURE_LONG_PRESS && ui_keyboard_release();
        if (ev->type != UI_GESTURE_TAP) return feedback ? password_paint(ctx, false, false) : APP_REDRAW_NONE;
        int x = ev->x0, y = ev->y0;
        if (ui_text_input_tap(&s_password_input, password_control_rect(47), UI_PX_BODY, !s_password_visible, NULL, x, y)) {
            return password_paint(ctx, true, false);
        }
        for (int i = 44; i <= 46; ++i) if (ui_rect_hit(password_control_rect(i), x, y)) {
            app_redraw_t changed = provisioning_action(i);
            if (changed == APP_REDRAW_AREA) return password_paint(ctx, true, true);
            return changed;
        }
        ui_keyboard_result_t result = ui_keyboard_tap(x, y, 560, ctx->now_ms);
        if (result == UI_KEYBOARD_DONE) {
            app_redraw_t changed = provisioning_action(46);
            if (changed == APP_REDRAW_PAGE) return changed;
            result = UI_KEYBOARD_CHANGED;
        }
        if (result == UI_KEYBOARD_CHANGED) return password_paint(ctx, false, y >= 990 && x >= 534);
        return APP_REDRAW_NONE;
    }
    int old = s_pressed;
    int origin = provisioning_hit(ev->x0, ev->y0);
    s_pressed = ev->type == UI_GESTURE_PRESS ? origin : -1;
    bool action = ev->type == UI_GESTURE_TAP && old >= 0 && old == origin && provisioning_hit(ev->x, ev->y) == origin;
    EpdRect old_rect = s_view == TRANSFER_PASSWORD ? password_control_rect(old >= 0 ? old : origin >= 0 ? origin : 0)
                                                  : network_control_rect(old >= 0 ? old : origin >= 0 ? origin : 0);
    if (action) {
        if (s_view == TRANSFER_PASSWORD && origin == 47) {
            ui_text_input_tap(&s_password_input, password_control_rect(47), UI_PX_BODY,
                              !s_password_visible, NULL, ev->x0, ev->y0);
            render(ctx, ctx->fb);
            s_area = password_control_rect(47);
            s_settle = false;
            return APP_REDRAW_AREA;
        }
        app_redraw_t redraw = provisioning_action(origin);
        if (redraw == APP_REDRAW_PAGE) return redraw;
        render(ctx, ctx->fb);
        s_area = ui_rect_union(old_rect, (EpdRect){UI_MARGIN, 240, ui_content_width(), 744});
        s_settle = false;
        return APP_REDRAW_AREA;
    }
    if (old == s_pressed) return APP_REDRAW_NONE;
    render(ctx, ctx->fb);
    s_area = old_rect;
    s_settle = false;
    return APP_REDRAW_AREA;
}

static EpdRect status_rect(void) { return (EpdRect){36, 676, 612, 270}; }
static uint64_t free_bytes(void* arg) { return book_store_free_bytes(arg); }

static void draw_status(uint8_t* fb) {
    EpdRect area = status_rect();
    ui_clear_rect_fast(fb, area);
    ui_fill_round_rect(fb, area, 24, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, area, 24, 0x98);
    const char* state = "网络已停止";
    if (s_start_pending || s_status.state == READ_PICO_TRANSFER_STARTING) state = "正在准备传书…";
    else if (s_status.state == READ_PICO_TRANSFER_READY) state = "等待上传";
    else if (s_status.state == READ_PICO_TRANSFER_UPLOADING) state = "正在接收";
    else if (s_status.state == READ_PICO_TRANSFER_ERROR) state = s_status.network_ready ? "上传失败，请重试" :
        s_mode == READ_PICO_TRANSFER_MODE_AP ? "热点传书未启动" : "WiFi 传书未启动";
    if (s_media_lost) state = "存储已移除，传书已停止";
    ui_text(fb, area.x + 28, area.y + 24, 26, state, EPD_DRAW_ALIGN_LEFT, false);
    char line[256];
    if (s_media_lost) snprintf(line, sizeof(line), "重新进入传书可使用内置存储");
    else if (s_status.state == READ_PICO_TRANSFER_ERROR && !s_status.network_ready)
        snprintf(line, sizeof(line), "原因：%s",
                 read_pico_transfer_rf_blocked() ? "飞行模式已开启，请先在设置中关闭"
                                                : esp_err_to_name(s_status.last_error));
    else if (s_mode == READ_PICO_TRANSFER_MODE_AP)
        snprintf(line, sizeof(line), "已连接 %u 台 · 已完成 %u 本", s_status.sta_count, s_status.done_count);
    else snprintf(line, sizeof(line), "同网浏览器上传 · 已完成 %u 本", s_status.done_count);
    ui_text(fb, area.x + 28, area.y + 68, 19, line, EPD_DRAW_ALIGN_LEFT, false);
    snprintf(line, sizeof(line), "%s", s_status.cur_name);
    while (*line && ttf_text_width_px(ui_text_effective_px(19), line) > area.width - 280) {
        size_t n = strlen(line) - 1;
        while (n && ((unsigned char)line[n] & 0xc0) == 0x80) --n;
        line[n] = 0;
    }
    ui_text(fb, area.x + 28, area.y + 108, 19, line[0] ? line : "尚无正在传输的文件", EPD_DRAW_ALIGN_LEFT, false);
    snprintf(line, sizeof(line), "%.1f / %.1f MB", s_status.cur_bytes / 1048576.0, s_status.cur_total / 1048576.0);
    ui_text(fb, area.x + area.width - 28, area.y + 108, 19, line, EPD_DRAW_ALIGN_RIGHT, false);
    EpdRect track = {area.x + 28, area.y + 150, area.width - 56, 10};
    ui_fill_round_rect(fb, track, 5, 0xc8);
    if (s_status.cur_total) {
        uint64_t width = (uint64_t)s_status.cur_bytes * track.width / s_status.cur_total;
        track.width = width > (uint64_t)track.width ? track.width : (int)width;
        ui_fill_round_rect(fb, track, 5, UI_GRAY_BLACK);
    }
    char capacity[112];
    if (s_media_lost) snprintf(capacity, sizeof(capacity), "TF 卡已移除，原存储已停用");
    else snprintf(capacity, sizeof(capacity), "%s · 剩余 %.1f MB%s", s_root.is_flash ? "内置" : "TF 卡",
                  s_free / 1048576.0, s_root.is_flash ? " · 单文件 ≤ 1 MB" : "");
    ui_text(fb, area.x + 28, area.y + 190, 18, capacity, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, area.x + 28, area.y + 228, 18,
            s_status.last_error != ESP_OK ? "传输未完成，请检查网络后重试" : "同名文件会覆盖；完成后自动更新书架",
            EPD_DRAW_ALIGN_LEFT, false);
}

static EpdRect control_rect(int id) {
    if (id == 0) return (EpdRect){36, 608, 296, 56};
    if (id == 1) return (EpdRect){352, 608, 296, 56};
    if (id == 2) return (EpdRect){36, 976, 612, 76};
    return (EpdRect){0};
}

/* ---- 手机推送武装页 / The phone-push arm page ---- */

// 武装入口的默认窗口：30 分钟。充电时常开（window 0）由页内按钮切换。
// The arm entry's default window: 30 minutes. Charger-held (window 0) is the
// in-page button's job.
#define PUSH_ENTRY_WINDOW_MS (30u * 60u * 1000u)

static const char *push_state_text(push_state_t st) {    switch (st) {
    case PUSH_STATE_ARMED: return "正在开启接收…";
    case PUSH_STATE_WINDOW: return "等待手机连接";
    case PUSH_STATE_CONNECTED: return "手机已连接";
    case PUSH_STATE_RECEIVING: return "正在接收推送…";
    case PUSH_STATE_PROCESSING: return "正在校验内容…";
    case PUSH_STATE_COOLDOWN: return "推送已完成";
    default: return "接收未开启";
    }
}

static bool push_charging_now(void) {
    const pmu_snapshot_t *p = read_pico_pmu_get();
    return p && p->present && pmu_battery_charging(p);
}

static void draw_push(uint8_t *fb) {
    ui_clear_page(fb);
    ui_fill_rect_fast((EpdRect){0, 0, UI_LOCK_WIDTH, UI_NAV_TOP}, 0xe0, fb);
    transfer_header(fb, "接收推送", NULL);

    // 广播名卡：手机在蓝牙列表里认的就是这一行。/ The adv-name card: this is the
    // line the phone picks from its Bluetooth list.
    EpdRect name = {36, 168, 612, 116};
    ui_fill_round_rect(fb, name, 24, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, name, 24, 0x98);
    const char *adv = push_sink_adv_name();
    char name_line[80];
    snprintf(name_line, sizeof(name_line), "%s", adv[0] ? adv : "正在生成名称…");
    fit_label(name_line, 30, 500);
    ui_text(fb, 64, 196, 30, name_line, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 64, 246, 19, "在手机蓝牙列表中选择这个名称", EPD_DRAW_ALIGN_LEFT, false);

    // 状态卡：状态、窗口/进度、进度条。/ The status card: state, window/progress
    // and the progress bar.
    EpdRect status = {36, 308, 612, 224};
    ui_fill_round_rect(fb, status, 24, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, status, 24, 0x98);
    const push_state_t st = push_sink_state();
    ui_text(fb, 64, 336, 27, push_state_text(st), EPD_DRAW_ALIGN_LEFT, false);
    char sub[128];
    uint32_t received = 0, total = 0;
    push_sink_progress(&received, &total);
    if (st == PUSH_STATE_RECEIVING && total) {
        snprintf(sub, sizeof(sub), "已接收 %.1f / %.1f KB", received / 1024.0, total / 1024.0);
    } else if (st == PUSH_STATE_COOLDOWN) {
        snprintf(sub, sizeof(sub), "共 %u 字节，可继续等待下一条", (unsigned)total);
    } else {
        const uint32_t left = push_sink_window_left_ms();
        snprintf(sub, sizeof(sub), "%s", left ? "窗口剩余时间见下方提示" : "充电时常开中，拔电即退出");
    }
    ui_text(fb, 64, 386, 19, sub, EPD_DRAW_ALIGN_LEFT, false);
    if (s_push_note[0]) {
        fit_label(s_push_note, 19, 520);
        ui_text(fb, 64, 424, 19, s_push_note, EPD_DRAW_ALIGN_LEFT, false);
    } else if (st == PUSH_STATE_RECEIVING && total) {
        EpdRect track = {64, 430, 556, 12};
        ui_fill_round_rect(fb, track, 6, 0xc8);
        uint64_t width = (uint64_t)received * track.width / total;
        if (width > (uint64_t)track.width) width = track.width;
        track.width = (int)width;
        ui_fill_round_rect(fb, track, 6, UI_GRAY_BLACK);
    }
    char window[96];
    const uint32_t left_ms = push_sink_window_left_ms();
    if (left_ms) snprintf(window, sizeof(window), "接收窗口剩余 %u 分 %02u 秒，超时自动退出",
                          (unsigned)(left_ms / 60000u), (unsigned)((left_ms / 1000u) % 60u));
    else snprintf(window, sizeof(window), "充电时常开：没有超时，拔下充电器即退出");
    ui_text(fb, 64, 486, 18, window, EPD_DRAW_ALIGN_LEFT, false);

    // 裁定 c 的明示文案：用户必须知道这会暂停翻页器。/ Ruling c's plain notice:
    // the user has to know this pauses the page-turner.
    ui_text(fb, 42, 556, 18, "开启后将暂停翻页器连接，退出接收后自动恢复。", EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 42, 586, 18, "接收期间屏幕不会自动锁屏，推送到会自动亮屏。", EPD_DRAW_ALIGN_LEFT, false);

    // 常开开关（仅插电时可切；传输中不切，避免 cancel 丢会话簿记）。
    // The hold switch (charger only; never mid-transfer, a cancel would drop the
    // session bookkeeping).
    EpdRect hold = control_rect(0);
    const bool held = push_sink_window_left_ms() == 0 && push_charging_now();
    const bool can_hold = push_charging_now() &&
        (st == PUSH_STATE_WINDOW || st == PUSH_STATE_CONNECTED);
    ui_fill_round_rect(fb, hold, 18, s_pressed == 0 ? 0x60 : (can_hold ? UI_GRAY_WHITE : 0xe8));
    ui_draw_round_rect(fb, hold, 18, can_hold ? 0x92 : 0xc0);
    ui_text_vc(fb, hold.x + hold.width / 2, hold.y + hold.height / 2, 20,
               held ? "常开已开启" : can_hold ? "切换充电时常开" : "常开需充电",
               EPD_DRAW_ALIGN_CENTER, can_hold);

    EpdRect back = control_rect(2);
    if (s_pressed == 2) ui_draw_pressed_round_rect(fb, back, UI_BTN_RADIUS);
    ui_draw_button(fb, back, "退出接收", false);
    ui_nav_draw(fb, 2);
    // 单词卡盖在武装页之上：模态卡片，卡外点击由 push_gesture 关掉。
    // The word card paints over the arm page: a modal card; taps outside are
    // closed by push_gesture.
    if (s_push_word_card) dict_popup_draw(fb);
}

// 错误码 → 用户看得懂的一句话。/ Error code to one plain sentence.
static const char *push_err_text(int code) {
    switch (code) {
    case PUSH_ERR_NOT_ARMED: return "接收未开启，请在设备上点开始";
    case PUSH_ERR_BAD_FRAME: return "内容格式不符，推送被拒绝";
    case PUSH_ERR_TOO_LARGE: return "内容超过 512 KB 上限";
    case PUSH_ERR_CHECKSUM: return "内容校验失败，请重新发送";
    case PUSH_ERR_NO_CARD: return "TF 卡不可用，无法保存";
    case PUSH_ERR_LOW_BATTERY: return "电量不足且未在充电";
    case PUSH_ERR_BUSY: return "上一条推送还没处理完";
    case PUSH_ERR_NO_DICT: return "设备上没有可用词典，单词卡已取消";
    default: return "推送失败，请重试";
    }
}

// 关掉单词卡并释放其内容。/ Close the word card and release its content.
static void push_word_card_close(void) {
    if (!s_push_word_card) return;
    s_push_word_card = false;
    dict_popup_reset();
}

// 武装成功后的陈事件清扫：closed/start_failed 只在会话走到 IDLE 或待武装超时
// 时补，若它们早于本次武装就是上一回合的遗物（离页 cancel、待武装失败后重进），
// 吃了它，别把用户扔回方法页。/ Stale-event sweep after a successful arm: closed
// and start_failed are posted only when a session reaches IDLE or a pending arm
// times out, so anything posted before this arm belongs to a previous round
// (leaving the page cancels; a failed pending arm followed by a re-entry).
// Swallow it instead of throwing the user back to the method page.
static void push_drain_stale_events(void) {
    (void)push_sink_take_closed();
    (void)push_sink_take_start_failed();
    (void)push_sink_take_open(NULL, 0, NULL);
    (void)push_sink_take_word_card(NULL, 0);
    (void)push_sink_take_err(NULL);
}

// 武装页的活区：广播名卡、状态卡与常开按钮——倒计时/进度/状态只在这三块变。
// The arm page's live regions: the adv-name card, the status card and the hold
// button - the countdown, progress and state only ever change in these three.
static EpdRect push_dynamic_rect(void) {
    return ui_rect_union(ui_rect_union((EpdRect){36, 168, 612, 116},
                                       (EpdRect){36, 308, 612, 224}),
                         control_rect(0));
}

// 把活区簿记对齐当前推流状态：调用方刚整页画过或刚武装时用，避免下一拍做
// 内容相同的重复刷新。/ Align the live-region bookkeeping with the current push
// state: for callers that just painted the full page or just armed, so the next
// tick does not repeat an identical refresh.
static void push_sync_live_bookkeeping(void) {
    s_push_last_state = push_sink_state();
    s_push_last_left_s = push_sink_window_left_ms() / 1000u;
    uint32_t received = 0, total = 0;
    push_sink_progress(&received, &total);
    s_push_last_recv = received;
    s_push_last_total = total;
    snprintf(s_push_last_adv, sizeof(s_push_last_adv), "%s", push_sink_adv_name());
}

// 武装页轮询：消费 push_sink 的一次性 UI 事件，500ms 节流刷新倒计时与进度。
// The arm page's pump: consume the push sink's one-shot UI events and refresh the
// countdown and progress at a 500 ms cadence.
static app_redraw_t push_tick(app_ctx_t *ctx) {
    // start_failed/closed 决定去留；open/word_card/err 只换页内文案。
    // start_failed/closed decide the page's fate; the rest swap the message.
    // start_failed 先于 closed 查：广播起不来时两个标志都会置位（CANCEL 的
    // ADV_OFF 顺带发 closed），「蓝牙启动失败，请重试」比裸回方法页信息量大，
    // 且两者互斥出现只可能是失败路径，正常收官只置 closed。
    // start_failed is checked before closed: when advertising cannot start,
    // both flags get set (CANCEL's ADV_OFF posts closed on the way out) and
    // the failure message is strictly more informative; the two coexist only
    // on the failure path - a normal close sets closed alone.
    if (push_sink_take_start_failed()) {
        push_word_card_close();
        (void)push_sink_take_closed(); // 吞掉同路的 closed，别闪两帧 / swallow the paired closed flag
        s_view = TRANSFER_METHODS;
        snprintf(s_method_message, sizeof(s_method_message), "蓝牙启动失败，请重试");
        return APP_REDRAW_PAGE;
    }
    if (push_sink_take_closed()) {
        push_word_card_close();
        s_view = TRANSFER_METHODS;
        // gen18：收摊必须带原因——窗口到期/拔电/飞行模式/USB 各说各的话，
        // 用户看得懂下一步该干什么（错误文案要能指路）。
        // gen18: a close-up must carry its reason - window expiry, unplug,
        // flight mode and USB each say their own piece, so the user knows
        // what to do next (error text must point the way).
        const char *why = push_sink_last_close_why();
        if (why) snprintf(s_method_message, sizeof(s_method_message), "%s", why);
        return APP_REDRAW_PAGE;
    }
    char title[PUSH_TITLE_MAX];
    uint8_t type = 0;
    if (push_sink_take_open(title, sizeof(title), &type)) {
        // 虚拟书：标题覆盖成推送标题、清掉旧进度，再切阅读页——「收完统一写
        // push.txt → 一次全刷」的全刷落在 app_book 的开书路径上，武装页不抢。
        // Virtual book: override the display title with the pushed one, drop
        // stale progress, then switch to the reader - the single full refresh
        // belongs to app_book's open path, so the arm page does not steal it.
        (void)book_title_set(PUSH_TEXT_PATH, title);
        (void)book_progress_forget(PUSH_TEXT_PATH);
        extern const app_desc_t app_book;
        if (app_book_request_open(PUSH_TEXT_PATH)) {
            // gen18（18-a）：开书即交接——标记这次离页，transfer_on_exit 据此
            // 不收摊；摊子由阅读器退出时（book_on_exit）才收。
            // gen18 (18-a): opening the book is the handoff - flag this exit so
            // transfer_on_exit keeps the arm; only leaving the reader closes up.
            s_push_handed_off = true;
            ctx->request_app = &app_book;
            return APP_REDRAW_NONE;
        }
        snprintf(s_push_note, sizeof(s_push_note), "打开推送内容失败，请重试");
        return APP_REDRAW_PAGE;
    }
    char word[PUSH_TITLE_MAX];
    if (push_sink_take_word_card(word, sizeof(word))) {
        // 单词卡：查词并开卡片；未收录或词典不在给一句准话，不黑屏不白卡。
        // Word card: look the word up and open the card; a miss or a missing
        // dictionary gets a definite sentence, never a blank card.
        dict_popup_set_anchor(0, false);
        if (dict_popup_open(word)) {
            s_push_word_card = true;
            return APP_REDRAW_PAGE;
        }
        if (dict_ready()) snprintf(s_push_note, sizeof(s_push_note), "未收录：%s", word);
        else snprintf(s_push_note, sizeof(s_push_note), "词典不可用，无法查词");
        return APP_REDRAW_PAGE;
    }
    int code = 0;
    if (push_sink_take_err(&code)) {
        snprintf(s_push_note, sizeof(s_push_note), "%s", push_err_text(code));
        return APP_REDRAW_PAGE;
    }
    if (ctx->now_ms - s_push_poll_ms < 500) return APP_REDRAW_NONE;
    s_push_poll_ms = ctx->now_ms;
    // 双保险：closed 事件万一漏接（极端时序），节流刷新也会把页面放回去。
    // A safety net: if the closed event were ever missed (extreme timing), the
    // throttled refresh still walks the page back.
    if (!push_sink_armed()) {
        push_word_card_close();
        s_view = TRANSFER_METHODS;
        return APP_REDRAW_PAGE;
    }
    // 活区没动就不刷：倒计时每秒才走一格，30 分钟窗口里每 500ms 一次全页 GL16
    // 等于几千次全闪，防闪烁不许这么干；有动就只重绘活区 + DU 区域更新。
    // Nothing moved, nothing to paint: the countdown steps once a second, so a
    // full GL16 every 500 ms would mean thousands of full flashes across a
    // 30-minute window - the no-flicker ruling forbids that. When something did
    // move, repaint only the live regions and present a DU area update.
    const push_state_t st = push_sink_state();
    const uint32_t left_s = push_sink_window_left_ms() / 1000u;
    uint32_t received = 0, total = 0;
    push_sink_progress(&received, &total);
    const char *adv = push_sink_adv_name();
    if (st == s_push_last_state && left_s == s_push_last_left_s &&
        received == s_push_last_recv && total == s_push_last_total &&
        !strcmp(adv, s_push_last_adv)) return APP_REDRAW_NONE;
    s_push_last_state = st;
    s_push_last_left_s = left_s;
    s_push_last_recv = received;
    s_push_last_total = total;
    snprintf(s_push_last_adv, sizeof(s_push_last_adv), "%s", adv);
    draw_push(ctx->fb);
    s_area = push_dynamic_rect();
    s_settle = false;
    return APP_REDRAW_AREA;
}

// 武装页手势：常开开关、退出接收、按下反馈。/ The arm page's gestures: the hold
// switch, the exit button and press feedback.
static app_redraw_t push_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    // 单词卡是模态的：开着时所有点击先问卡片——chip 命中切换保存、卡片留着，
    // 其余点击关卡片；按下反馈一律吞掉（卡片盖着的按钮不给人按的错觉）。
    // The word card is modal: while it is open every tap asks the card first -
    // a chip hit toggles the save and keeps the card, any other tap closes it;
    // press feedback is swallowed outright (no illusion of pressing a button
    // the card covers).
    if (s_push_word_card) {
        // 非点击不动屏幕（按下反馈一律吞掉：卡片盖着的按钮不给人按的错觉）；
        // 点击才问 chip，命中切换保存、未命中关卡片，两者都要重绘。
        // Non-taps change nothing on screen (press feedback is swallowed: no
        // illusion of pressing a button the card covers); only a tap asks the
        // chip - a hit toggles the save, a miss closes the card - and either
        // way the page repaints once.
        if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
        (void)dict_popup_tap(ev->x0, ev->y0);
        push_word_card_close();
        s_pressed = -1;
        return APP_REDRAW_PAGE;
    }
    int old = s_pressed;
    int origin = -1;
    if (ui_rect_hit(control_rect(0), ev->x0, ev->y0)) origin = 0;
    else if (ui_rect_hit(control_rect(2), ev->x0, ev->y0)) origin = 2;
    s_pressed = ev->type == UI_GESTURE_PRESS ? origin : -1;
    if (ev->type != UI_GESTURE_TAP || old < 0 || old != origin) {
        if (old == s_pressed) return APP_REDRAW_NONE;
        render(ctx, ctx->fb);
        s_settle = false;
        s_area = control_rect(old >= 0 ? old : s_pressed);
        return APP_REDRAW_AREA;
    }
    if (origin == 0) {
        // 充电时常开：cancel 当前窗口再以 window 0 重新武装。仅 WINDOW/CONNECTED
        // 可切（draw_push 已置灰，这里再挡一次）；传输中切会丢会话簿记。
        // Charger-held: cancel the window and re-arm with window 0. Only from
        // WINDOW/CONNECTED (draw_push greys the button; checked again here) -
        // switching mid-transfer would drop the session bookkeeping.
        if (!push_charging_now()) return APP_REDRAW_NONE;
        const push_state_t st = push_sink_state();
        if (st != PUSH_STATE_WINDOW && st != PUSH_STATE_CONNECTED) return APP_REDRAW_NONE;
        push_sink_cancel();
        if (push_sink_arm(0, 0) != ESP_OK) {
            // cancel 之后 closed 会把页面带回方法页，原因写到方法页的消息位上，
            // 别只留在武装页的便签里。/ After the cancel the closed event walks
            // the page back to the method page, so the reason goes to the
            // method page's message slot, not just the arm page's note.
            snprintf(s_method_message, sizeof(s_method_message), "切换常开失败");
            snprintf(s_push_note, sizeof(s_push_note), "切换常开失败");
            return APP_REDRAW_PAGE;
        }
        // cancel 补的 closed 是旧会话的收尾信号，重武装是有意为之：吃掉它，
        // 否则下一拍就把用户扔回方法页。/ The closed posted by the cancel ends
        // the old session; the re-arm is deliberate: swallow the event, or the
        // next tick would throw the user back to the method page.
        push_drain_stale_events();
        s_push_note[0] = 0;
        // 全页重画已经画上新 token，簿记对齐当前值，别让 500ms 后再补一次
        // 内容相同的 DU 区域刷新。/ The full repaint already shows the new
        // token: align the bookkeeping so the 500 ms tick does not repeat an
        // identical DU area refresh.
        push_sync_live_bookkeeping();
        return APP_REDRAW_PAGE;
    }
    // 退出接收：cancel 之后 closed 事件会把页面带回方法页。
    // Exit: after the cancel the closed event walks the page back.
    push_sink_cancel();
    return APP_REDRAW_NONE;
}

static void render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    if (s_usb_entry) {
        ui_clear_page(fb);
        bool active = usb_storage_active();
        transfer_header(fb, "USB 读卡", NULL);
        ui_text(fb, 42, 181, 20, "连接状态", EPD_DRAW_ALIGN_LEFT, false);
        EpdRect status = {36, 211, 612, 190};
        ui_fill_round_rect(fb, status, 24, UI_GRAY_WHITE);
        ui_draw_round_rect(fb, status, 24, 0x90);
        const char *state = s_usb_start_pending ? "正在准备 TF 卡" :
            !active ? "USB 读卡未开启" :
            s_usb_host_connected ? "电脑已连接" : "等待电脑连接";
        ui_text(fb, 64, 241, 31, state, EPD_DRAW_ALIGN_LEFT, false);
        if (active) {
            ui_text(fb, 64, 296, 20, "磁盘名称  SD_CARD", EPD_DRAW_ALIGN_LEFT, false);
            char size[64];
            snprintf(size, sizeof(size), "TF 卡容量  %.1f GB", s_usb_capacity_bytes / 1073741824.0);
            ui_text(fb, 64, 337, 20, size, EPD_DRAW_ALIGN_LEFT, false);
        } else ui_text(fb, 64, 309, 20, s_usb_message, EPD_DRAW_ALIGN_LEFT, false);

        ui_text(fb, 42, 450, 20, "文件存放位置", EPD_DRAW_ALIGN_LEFT, false);
        ui_fill_round_rect(fb, (EpdRect){36, 478, 612, 206}, 24, UI_GRAY_WHITE);
        ui_text(fb, 64, 510, 27, "books", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 64, 553, 19, "EPUB、TXT 书籍", EPD_DRAW_ALIGN_LEFT, false);
        ui_hairline(fb, 580, 64, 556, 0xb0);
        ui_text(fb, 64, 597, 27, "fonts", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 64, 640, 19, "TTF、OTF 字体", EPD_DRAW_ALIGN_LEFT, false);

        ui_text(fb, 42, 740, 20, "完成传输", EPD_DRAW_ALIGN_LEFT, false);
        ui_fill_round_rect(fb, (EpdRect){36, 766, 612, 147}, 24, UI_GRAY_WHITE);
        ui_text(fb, 64, 802, 21, "1  在电脑上等待复制完成", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 64, 855, 21, "2  安全弹出 SD_CARD，再结束读卡", EPD_DRAW_ALIGN_LEFT, false);
        ui_fill_round_rect(fb, control_rect(2), 22, s_pressed == 2 ? 0x60 : 0x38);
        ui_text_vc(fb, 342, 1014, 24, active ? "结束 USB 读卡" : "返回文件管理",
                   EPD_DRAW_ALIGN_CENTER, true);
        return;
    }
    if (s_view == TRANSFER_METHODS) { draw_methods(fb); return; }
    if (s_view == TRANSFER_NETWORKS) { draw_networks(fb); return; }
    if (s_view == TRANSFER_PASSWORD) { draw_password(fb); return; }
    if (s_view == TRANSFER_PUSH) { draw_push(fb); return; }
    ui_clear_page(fb);
    transfer_header(fb, s_mode == READ_PICO_TRANSFER_MODE_AP ? "热点传书" : "WiFi 传书", NULL);
    EpdRect connection = {36, 168, 612, 132};
    ui_fill_round_rect(fb, connection, 24, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, connection, 24, 0x98);
    char name[64];
    snprintf(name, sizeof(name), "%s", s_mode == READ_PICO_TRANSFER_MODE_AP ? s_status.ssid : s_status.wifi_ssid);
    if (!name[0]) snprintf(name, sizeof(name), "%s", s_mode == READ_PICO_TRANSFER_MODE_AP ? "kiikoread 热点" : "已保存的 WiFi");
    fit_label(name, 26, 420);
    ui_text(fb, 66, 190, 26, name, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 66, 236, 19,
            s_mode == READ_PICO_TRANSFER_MODE_AP ? "口令：" READ_PICO_TRANSFER_PASSWORD :
            s_status.network_ready ? "设备已连接；手机需同网" :
            s_start_pending || s_status.state == READ_PICO_TRANSFER_STARTING ? "正在连接已保存网络…" :
            s_status.wifi_configured ? "已保存，尚未连接" : "尚未选择 WiFi",
            EPD_DRAW_ALIGN_LEFT, false);
    ui_text_vc(fb, 605, 234, 20, s_status.network_ready ? "服务已开启" : "准备中", EPD_DRAW_ALIGN_RIGHT, false);
    int url_px = UI_PX_CAPTION;
    while (url_px > 14 && ttf_text_width_px(url_px, s_status.url) > 560) --url_px;
    if (s_status.network_ready && s_qr_ready)
        ui_wifi_qr_draw(fb, (EpdRect){222, 330, 240, 240});
    else {
        ui_draw_round_rect(fb, (EpdRect){222, 330, 240, 240}, 8, 0xa0);
        ui_text_vc(fb, 342, 450, 22, s_start_pending ? "正在生成二维码…" : "等待网络地址…", EPD_DRAW_ALIGN_CENTER, false);
    }
    if (s_mode == READ_PICO_TRANSFER_MODE_AP) {
        ui_text_vc(fb, 342, 583, 20,
                   s_status.network_ready ? s_status.url : "热点就绪后显示访问地址",
                   EPD_DRAW_ALIGN_CENTER, false);
        for (int i = 0; i < 2; ++i) {
            EpdRect tab = control_rect(i);
            ui_fill_round_rect(fb, tab, 18, (s_qr_url == (i == 1)) ? 0x42 : UI_GRAY_WHITE);
            ui_draw_round_rect(fb, tab, 18, 0x92);
            ui_text_vc(fb, tab.x + tab.width / 2, tab.y + tab.height / 2, 21,
                       i ? "打开传书网页" : "连接 kiikoread 热点", EPD_DRAW_ALIGN_CENTER, s_qr_url == (i == 1));
        }
    } else {
        ui_text_vc(fb, 342, 602, url_px,
                   s_status.network_ready ? s_status.url : "服务启动后显示访问地址", EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 642, 18, "手机或电脑扫码后，即可在浏览器上传内容", EPD_DRAW_ALIGN_CENTER, false);
    }
    draw_status(fb);
    EpdRect back = control_rect(2);
    if (s_pressed == 2) ui_draw_pressed_round_rect(fb, back, UI_BTN_RADIUS);
    ui_draw_button(fb, back,
                   s_mode == READ_PICO_TRANSFER_MODE_AP && s_status.state == READ_PICO_TRANSFER_ERROR && !s_status.network_ready
                       ? "重试热点传书" : "停止传输服务", false);
    ui_nav_draw(fb, 2);
}

// gen19（真机 Bug2）：武装动作收敛成助手，进场（on_enter）与页内点击
// （method_gesture）共用。gen18 只让 on_enter 消费 s_push_requested——用户已在
// 本页内点「手机推送」时 on_enter 不会重跑，flag 挂死、按钮「每次都没反应」，
// 只有退出重进才生效。页面已显示，武装当场执行。
// gen19 (real-hw Bug2): the arming move lives in this helper, shared by entry
// (on_enter) and the in-page tap (method_gesture). gen18 let only on_enter
// consume s_push_requested - tapping "phone push" while already on this page
// never re-ran on_enter, so the flag hung and the button did nothing until a
// leave-and-reenter. The page is already showing: arm right now.
static void push_arm_now(void) {
    const esp_err_t err = push_sink_arm(0, PUSH_ENTRY_WINDOW_MS);
    if (err == ESP_ERR_NOT_ALLOWED) {
        snprintf(s_method_message, sizeof(s_method_message), "电量不足，无法开启接收");
        return;
    }
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        snprintf(s_method_message, sizeof(s_method_message), "接收开启失败（%d）", err);
        return;
    }
    s_view = TRANSFER_PUSH;
    // 新回合干净开局：单词卡收掉、陈事件清扫（离页补的 closed、上一回合
    // 待武装失败补的 start_failed 等）、活区簿记对齐当前值。
    // A clean slate for the new round: drop the word card, sweep stale events
    // and align the live-region bookkeeping.
    push_word_card_close();
    push_drain_stale_events();
    push_sync_live_bookkeeping();
}

static void on_enter(app_ctx_t* ctx) {
    (void)ctx;
    display_set_bulk_io(true);
    s_media_lost = false;
    memset(&s_status, 0, sizeof(s_status));
    s_qr_url = s_mode == READ_PICO_TRANSFER_MODE_STA;
    clear_qr();
    memset(&s_root, 0, sizeof(s_root));
    s_heap_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    s_internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_start_pending = false;
    s_usb_message[0] = 0;
    s_usb_start_pending = false;
    s_usb_retry_ms = 0;
    s_usb_host_connected = false;
    s_usb_capacity_bytes = 0;
    s_method_message[0] = 0;
    s_push_note[0] = 0;
    s_push_poll_ms = 0;
    s_push_handed_off = false; // gen18：进场即清交接标记，旧标记不过夜 / gen18: clear the handoff flag on entry - no stale flags overnight
    s_view = TRANSFER_METHODS;
    s_direct_entry = s_usb_requested || s_wifi_upload_requested || s_hotspot_requested;
    s_usb_entry = s_usb_requested;
    s_forget_confirm = false;
    s_scan_pending = false;
    s_network_connect_pending = false;
    clear_password();
    s_session_started = false;
    s_pressed = -1;
    s_settle = s_any_changed = false;
    s_status.mode = s_mode;
    read_pico_transfer_get_saved_wifi(s_status.wifi_ssid, &s_status.wifi_configured);
    s_poll_ms = 0;
    s_free = 0;
    if (s_usb_requested) {
        read_pico_sd_info_t usb_sd = {0};
        if (read_pico_sd_get_info(&usb_sd) == ESP_OK) s_usb_capacity_bytes = usb_sd.capacity_bytes;
        s_usb_requested = false;
        s_view = TRANSFER_HOME;
        s_usb_start_pending = true;
        snprintf(s_usb_message, sizeof(s_usb_message), "正在卸载本机 TF 卡并启动 USB…");
    } else if (s_hotspot_requested) {
        s_hotspot_requested = false;
        if (!start_hotspot_service()) {
            s_view = TRANSFER_HOME;
            s_status.state = READ_PICO_TRANSFER_ERROR;
            snprintf(s_method_message, sizeof(s_method_message), "传输正在结束，请稍后再试");
        }
    } else if (s_wifi_upload_requested) {
        s_wifi_upload_requested = false;
        // 配网页可能留下常驻的仅联网会话；传书前先释放它再启动上传服务。
        // Provisioning may leave a network-only session alive; release it before starting upload.
        if (s_status.wifi_configured && read_pico_transfer_try_stop_if_idle())
            queue_network_start(READ_PICO_TRANSFER_MODE_STA);
        else if (s_status.wifi_configured) {
            s_view = TRANSFER_HOME;
            s_status.state = READ_PICO_TRANSFER_ERROR;
            snprintf(s_method_message, sizeof(s_method_message), "传输正在结束，请稍后再试");
        }
        else if (!enter_networks()) {
            s_view = TRANSFER_NETWORKS;
            s_scan_pending = false;
            snprintf(s_network_message, sizeof(s_network_message), "网络正在结束，请稍后再试");
        }
    } else if (s_wifi_setup_requested) {
        s_wifi_setup_requested = false;
        read_pico_transfer_get_status(&s_status);
        if (s_status.network_ready && s_status.mode == READ_PICO_TRANSFER_MODE_STA) {
            s_session_started = true;
            s_view = TRANSFER_NETWORKS;
            s_saved_configured = s_status.wifi_configured;
            snprintf(s_saved_ssid, sizeof(s_saved_ssid), "%s", s_status.wifi_ssid);
            snprintf(s_network_message, sizeof(s_network_message), "连接成功");
        } else {
            (void)enter_networks();
        }
    } else if (s_push_requested) {
        // 手机推送入口：直接武装（push_arm_now）。电量不足（组件层门槛）或被
        // 别的会话占住时退回方法页并说明原因。
        // The phone-push entry: arm outright via push_arm_now. A low battery
        // (the glue-side gate) or a busy session falls back to the method page
        // with the reason shown.
        s_push_requested = false;
        push_arm_now();
    } else if (push_sink_armed()) {
        // gen18（18-a 自动切回落点）：sink 正被后台保活——用户在读 push.txt，
        // 手机回连把用户从阅读器弹回本页。直接落武装视图接客，**不重新武装**
        // （会话是活的，ERR(1) 意味着保活失败，验收要抓的就是它）。
        // gen18 (18-a auto-switch-back landing): the sink is background-alive -
        // the user reads push.txt and a returning phone bounced them back here.
        // Land straight on the arm view to receive and **do not re-arm** (the
        // session is live; an ERR(1) would mean the keep-alive failed, which is
        // exactly what acceptance watches for).
        s_view = TRANSFER_PUSH;
        push_word_card_close();
        push_sync_live_bookkeeping();
    } else {
        s_wifi_settings_only = false;
    }
}

static void stop_session(void) {
    clear_qr();
    // 停服后读最终计数，保留本页跨模式文件变更的记录。
    // Join reception before reading counts and retain file changes across mode switches.
    read_pico_transfer_stop();
    read_pico_transfer_get_status(&s_status);
    if (s_session_started && s_status.changed_count) s_any_changed = true;
    s_session_started = false;
}

static bool stop_upload_if_idle(void) {
    if (!read_pico_transfer_try_stop_if_idle()) return false;
    stop_session();
    s_start_pending = false;
    return true;
}

// 必须先等待 HTTP 退出，再清除旧根；不会让在途请求写到另一存储。
// Join HTTP before clearing the old root; in-flight requests never move to another storage.
static void transfer_on_media_lost(app_ctx_t* ctx) {
    (void)ctx;
    if (s_root.is_flash) return;
    if (s_view == TRANSFER_PUSH) push_word_card_close();
    // gen18：媒体丢失照旧收摊（裁决口径）——卡没了，后续 BEGIN 只会在
    // part_open_append 撞 ERR(5)，不如诚实地结束并留痕。
    // gen18: media loss closes up as ruled - with the card gone every later
    // BEGIN just hits ERR(5) in part_open_append, so end it honestly with a trace.
    if (push_sink_armed() || push_sink_state() != PUSH_STATE_IDLE) {
        push_sink_cancel_why("TF 卡已拔出，接收结束");
    }
    stop_session();
    s_start_pending = s_scan_pending = false;
    s_view = s_direct_entry ? TRANSFER_HOME : TRANSFER_METHODS;
    clear_password();
    s_pressed = -1;
    s_free = 0;
    memset(&s_root, 0, sizeof(s_root));
    s_media_lost = true;
    s_settle = false;
}

static void transfer_on_exit(app_ctx_t* ctx) {
    (void)ctx;
    s_usb_start_pending = false;
    // gen19（真机 Bug3）：离页一律不收摊。保活是产品语义——回到首页状态卡
    // 继续显示「等待连接 · 剩 N 分钟」，会话由窗口到期自然收（≤30 分钟），
    // 或系统级入口收（飞行模式/USB/关机/媒体丢失/拔电）。真机实证：推单词卡
    // 后离页走了旧 cancel，状态卡直接「未开启」，与长文开书路径行为分裂。
    // gen19 (real-hw Bug3): leaving the page never tears the session down.
    // Keep-alive is the product semantics - the home status card keeps showing
    // "waiting - N min left"; the window expiry closes it naturally (<=30 min)
    // and system-level entries (flight mode / USB / shutdown / media lost /
    // unplug) close it explicitly. Real-hw proof: the word-card exit hit the
    // old cancel and the card flipped to "off", diverging from the long-text
    // handoff path.
    s_push_handed_off = false;
    if (s_view == TRANSFER_PUSH) push_word_card_close();
    if (usb_storage_active()) {
        esp_err_t err = usb_storage_stop();
        if (err == ESP_OK) s_any_changed = true;
        else ESP_LOGW(TAG, "USB remained active on page exit: %s", esp_err_to_name(err));
    }
    clear_password();
    s_scan_pending = s_network_connect_pending = false;
    read_pico_transfer_status_t latest = {0};
    read_pico_transfer_get_status(&latest);
    bool keep_sta = s_wifi_settings_only && latest.network_ready &&
                    latest.mode == READ_PICO_TRANSFER_MODE_STA;
    if (keep_sta) {
        clear_qr();
        s_session_started = false; // 全局循环接管保活；再次进页会从组件状态恢复。
    } else stop_session();
    s_wifi_settings_only = false;
    s_usb_entry = false;
    s_direct_entry = false;
    display_set_bulk_io(false);
    if (s_any_changed) book_store_notify_changed();
    ESP_LOGI(TAG, "leave keep_sta=%d heap before=%u after=%u internal_before=%u internal_after=%u",
             keep_sta,
             (unsigned)s_heap_before, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)s_internal_before, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

static app_redraw_t on_tick(app_ctx_t* ctx) {
    if (s_view == TRANSFER_PASSWORD) {
        if (ctx->consumed) return APP_REDRAW_NONE;
        bool held = !ctx->released && ctx->touch && ctx->touch->touched && ctx->touch->count == 1;
        if (ui_keyboard_hold_tick(held, ctx->touch ? ctx->touch->x : 0,
            ctx->touch ? ctx->touch->y : 0, ctx->now_ms) == UI_KEYBOARD_CHANGED) return password_paint(ctx, false, false);
        if (ui_keyboard_idle_tick(ctx->touch && ctx->touch->touched, ctx->now_ms) == UI_KEYBOARD_CHANGED)
            return password_paint(ctx, false, false);
    }
    uint32_t revision = __atomic_load_n(&s_signature_revision, __ATOMIC_ACQUIRE);
    if (revision != s_signature_seen) {
        s_signature_seen = revision;
        return APP_REDRAW_PAGE;
    }
    if (s_usb_entry) {
        if (!s_usb_start_pending) {
            bool connected = usb_storage_connected();
            if (connected == s_usb_host_connected) return APP_REDRAW_NONE;
            s_usb_host_connected = connected;
            return APP_REDRAW_PAGE;
        }
        if (s_usb_retry_ms && ctx->now_ms - s_usb_retry_ms < 250) return APP_REDRAW_NONE;
        s_usb_retry_ms = ctx->now_ms;
        esp_err_t err = usb_storage_start();
        if (err == ESP_ERR_NOT_FINISHED) return APP_REDRAW_NONE;
        s_usb_start_pending = false;
        s_usb_host_connected = usb_storage_connected();
        if (err != ESP_OK)
            snprintf(s_usb_message, sizeof(s_usb_message), "USB 模式启动失败：%s", esp_err_to_name(err));
        return APP_REDRAW_PAGE;
    }
    if (usb_storage_active()) return APP_REDRAW_NONE;
    if (s_view == TRANSFER_PUSH) return push_tick(ctx);
    if (s_view == TRANSFER_METHODS) return APP_REDRAW_NONE;
    if (s_view != TRANSFER_HOME) return network_ui_tick(ctx);
    if (s_start_pending) {
        s_start_pending = false;
        // 传书模式也应在启动无线电前释放可重建字形，避免内部内存不足。
        // Release rebuildable glyphs before starting the radio for uploads too.
        ttf_font_cache_clear();
        esp_err_t err = book_store_upload_root(&s_root);
        if (err == ESP_OK) {
            s_media_lost = false;
            s_free = book_store_free_bytes(&s_root);
            read_pico_transfer_cfg_t cfg = {.mode = s_mode, .root_dir = s_root.path, .is_flash = s_root.is_flash,
                .file_limit = book_store_file_limit(&s_root), .free_bytes_cb = free_bytes, .free_bytes_ctx = &s_root,
                .file_changed_cb = book_progress_forget, .file_deleted_cb = transfer_file_deleted,
                .file_moved_cb = transfer_file_moved,
                .directory_deleted_cb = transfer_directory_deleted,
                .directory_moved_cb = transfer_directory_moved,
                .wallpaper_set_cb = transfer_set_wallpaper,
                .signature_get_cb = transfer_signature_get, .signature_set_cb = transfer_signature_set,
                .title_get_cb = NULL, .title_set_cb = NULL};
            err = read_pico_transfer_start(&cfg);
            s_session_started = err == ESP_OK;
            read_pico_transfer_get_status(&s_status);
            if (err == ESP_OK) s_free = book_store_free_bytes(&s_root);
        }
        s_status.mode = s_mode;
        if (err != ESP_OK) { s_status.state = READ_PICO_TRANSFER_ERROR; s_status.last_error = err; }
        if (err == ESP_OK) prepare_qr();
        else clear_qr();
        ESP_LOGI(TAG, "start root=%s result=%s", s_root.path, esp_err_to_name(err));
        return APP_REDRAW_PAGE;
    }
    if (ctx->consumed || ctx->now_ms - s_poll_ms < 2000) return APP_REDRAW_NONE;
    s_poll_ms = ctx->now_ms;
    read_pico_transfer_service_poll();
    read_pico_transfer_status_t next = {0};
    read_pico_transfer_get_status(&next);
    // 接收期间让屏幕保持静止，网页端显示实时进度；避免刷屏与写卡争用。
    // Keep the panel still during reception; the browser owns live progress.
    if (next.state == READ_PICO_TRANSFER_UPLOADING && s_status.state == READ_PICO_TRANSFER_UPLOADING) {
        s_status.cur_bytes = next.cur_bytes;
        s_status.sta_count = next.sta_count;
        return APP_REDRAW_NONE;
    }
    if (s_status.state == READ_PICO_TRANSFER_ERROR && next.state == READ_PICO_TRANSFER_STOPPED) return APP_REDRAW_NONE;
    bool network_changed = next.network_ready != s_status.network_ready ||
        next.mode != s_status.mode || strcmp(next.ssid, s_status.ssid) ||
        next.wifi_configured != s_status.wifi_configured || strcmp(next.url, s_status.url) ||
        strcmp(next.wifi_ssid, s_status.wifi_ssid);
    if (!network_changed && next.state == s_status.state && next.sta_count == s_status.sta_count &&
        next.cur_bytes == s_status.cur_bytes && next.cur_total == s_status.cur_total &&
        next.done_count == s_status.done_count && next.changed_count == s_status.changed_count && next.last_error == s_status.last_error &&
        !strcmp(next.cur_name, s_status.cur_name)) return APP_REDRAW_NONE;
    s_settle = next.changed_count != s_status.changed_count || next.done_count != s_status.done_count ||
        (s_status.state == READ_PICO_TRANSFER_UPLOADING && next.state != READ_PICO_TRANSFER_UPLOADING);
    s_status = next;
    if (network_changed) prepare_qr();
    if (s_settle) {
        s_free = book_store_free_bytes(&s_root);
        draw_status(ctx->fb);
        s_area = status_rect();
        return APP_REDRAW_AREA;
    }
    if (network_changed) { s_free = book_store_free_bytes(&s_root); return APP_REDRAW_PAGE; }
    draw_status(ctx->fb);
    s_area = status_rect();
    return APP_REDRAW_AREA;
}

static int hit_control(uint16_t x, uint16_t y) {
    if (usb_storage_active()) return ui_rect_hit(control_rect(2), x, y) ? 2 : -1;
    if (s_mode == READ_PICO_TRANSFER_MODE_AP && s_view == TRANSFER_HOME)
        for (int i = 0; i < 2; ++i) if (ui_rect_hit(control_rect(i), x, y)) return i;
    return ui_rect_hit(control_rect(2), x, y) ? 2 : -1;
}

static int hit_method(uint16_t x, uint16_t y) {
    for (int i = 0; i < 3; ++i) if (ui_rect_hit(method_control_rect(i), x, y)) return i;
    return -1;
}

static app_redraw_t stop_usb_for_navigation(app_ctx_t *ctx, bool home) {
    s_usb_start_pending = false;
    if (usb_storage_active()) {
        esp_err_t err = usb_storage_stop();
        if (err != ESP_OK) {
            snprintf(s_usb_message, sizeof(s_usb_message), "USB 写入未完成：%s，请在电脑弹出后重试", esp_err_to_name(err));
            return APP_REDRAW_PAGE;
        }
        s_any_changed = true;
    }
    if (home) ui_nav_request(ctx, 0);
    else if (s_direct_entry) ctx->request_return = true;
    else s_view = TRANSFER_METHODS;
    return ctx->request_return || home ? APP_REDRAW_NONE : APP_REDRAW_PAGE;
}

static app_redraw_t method_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    int old = s_pressed;
    int origin = hit_method(ev->x0, ev->y0);
    s_pressed = ev->type == UI_GESTURE_PRESS ? origin : -1;
    if (ev->type == UI_GESTURE_TAP && old >= 0 && old == origin && hit_method(ev->x, ev->y) == origin) {
        if (origin == 0) {
            if (s_status.wifi_configured) {
                if (stop_upload_if_idle()) {
                    queue_network_start(READ_PICO_TRANSFER_MODE_STA);
                    s_method_message[0] = 0;
                    return APP_REDRAW_PAGE;
                }
            } else if (enter_networks()) return APP_REDRAW_PAGE;
            snprintf(s_method_message, sizeof(s_method_message), "传输正在结束，请稍后再试");
        } else if (origin == 1) {
            if (start_hotspot_service()) return APP_REDRAW_PAGE;
            snprintf(s_method_message, sizeof(s_method_message), "传输正在结束，请稍后再试");
        } else if (origin == 2) {
            // 手机推送：gen19（真机 Bug2）改为当场武装（push_arm_now）。
            // gen18 只置 flag 等 on_enter 消费——用户已在页内，on_enter 不会
            // 重跑，flag 挂死按钮无反应。/ Phone push: gen19 (real-hw Bug2)
            // arms right now via push_arm_now; gen18's flag-for-on_enter hung
            // forever because on_enter never re-runs while the page is open.
            s_push_requested = false;
            push_arm_now();
        }
        return APP_REDRAW_PAGE;
    }
    if (old == s_pressed) return APP_REDRAW_NONE;
    // 按下反馈只刷新单行；整页刷新会取消尚未松手的手势，导致入口无法点击。
    // Refresh only the pressed row; a page redraw cancels the active gesture before its tap.
    render(ctx, ctx->fb);
    s_area = method_control_rect(old >= 0 ? old : s_pressed);
    s_settle = false;
    return APP_REDRAW_AREA;
}

static app_redraw_t on_gesture(app_ctx_t* ctx, const ui_gesture_event_t* ev) {
    if (s_status.state == READ_PICO_TRANSFER_UPLOADING) return APP_REDRAW_NONE;
    if (ev->type == UI_GESTURE_TAP && s_view != TRANSFER_PASSWORD && !usb_storage_active()) {
        int tab = ui_nav_hit(ev->x0, ev->y0);
        if (tab >= 0 && tab == ui_nav_hit(ev->x, ev->y)) {
            if (s_view == TRANSFER_HOME && !stop_upload_if_idle()) return APP_REDRAW_NONE;
            // 武装页先收摊再切走：BLE 栈该跟着翻页器设置走，不该为看不见的
            // 推流续着。/ Leave the arm before navigating away: the BLE stack
            // follows the page-turner setting, not a push no longer on screen.
            // gen19（Bug3）：切页不再收摊，保活语义统一（见 transfer_on_exit）。
            // gen19 (Bug3): navigation no longer cancels; keep-alive semantics
            // are unified (see transfer_on_exit).
            if (s_view == TRANSFER_PUSH) push_word_card_close();
            s_pressed = -1;
            ui_nav_request(ctx, tab);
            return APP_REDRAW_NONE;
        }
    }
    if (ev->type == UI_GESTURE_TAP && ev->y0 < 160 && ev->x0 < 120) {
        if (s_usb_entry || usb_storage_active()) return stop_usb_for_navigation(ctx, false);
        if (s_view == TRANSFER_PASSWORD) { clear_password(); s_view = TRANSFER_NETWORKS; return APP_REDRAW_PAGE; }
        // gen19（Bug3）：接收页返回不再收摊（保活语义统一）；词卡收掉即可。
        // gen19 (Bug3): back from the push view no longer cancels (unified
        // keep-alive semantics); just drop the word card.
        if (s_view == TRANSFER_PUSH) { push_word_card_close(); s_view = TRANSFER_METHODS; return APP_REDRAW_PAGE; }
        if (s_view == TRANSFER_NETWORKS) {
            if (s_wifi_settings_only) { ctx->request_return = true; return APP_REDRAW_NONE; }
            if (s_direct_entry) { ctx->request_return = true; return APP_REDRAW_NONE; }
            s_view = TRANSFER_METHODS; return APP_REDRAW_PAGE;
        }
        if (s_view == TRANSFER_HOME) {
            if (!stop_upload_if_idle()) return APP_REDRAW_NONE;
            if (s_direct_entry) { ctx->request_return = true; return APP_REDRAW_NONE; }
            s_view = TRANSFER_METHODS;
            return APP_REDRAW_PAGE;
        }
        ctx->request_return = true;
        return APP_REDRAW_NONE;
    }
    if (s_view == TRANSFER_METHODS) return method_gesture(ctx, ev);
    if (s_view == TRANSFER_PUSH) return push_gesture(ctx, ev);
    if (s_view != TRANSFER_HOME) return provisioning_gesture(ctx, ev);
    int old = s_pressed;
    int origin = hit_control(ev->x0, ev->y0);
    s_pressed = ev->type == UI_GESTURE_PRESS ? origin : -1;
    if (ev->type == UI_GESTURE_TAP && old >= 0 && old == origin &&
        hit_control(ev->x, ev->y) == origin) {
        if (s_usb_entry) {
            return stop_usb_for_navigation(ctx, false);
        }
        if (origin == 0 || origin == 1) {
            bool url = origin == 1;
            if (s_qr_url != url) {
                s_qr_url = url;
                prepare_qr();
                return APP_REDRAW_PAGE;
            }
            return APP_REDRAW_NONE;
        }
        if (origin == 2) {
            if (s_mode == READ_PICO_TRANSFER_MODE_AP && s_status.state == READ_PICO_TRANSFER_ERROR &&
                !s_status.network_ready && start_hotspot_service()) return APP_REDRAW_PAGE;
            // 即使没有切页历史也先停服，取消尚未执行的启动请求。
            // Stop even without navigation history and cancel a queued start.
            if (!stop_upload_if_idle()) return APP_REDRAW_NONE;
            if (s_direct_entry) { ctx->request_return = true; return APP_REDRAW_NONE; }
            s_view = TRANSFER_METHODS;
            return APP_REDRAW_PAGE;
        }
    }
    if (s_pressed == old) return APP_REDRAW_NONE;
    render(ctx, ctx->fb);
    s_settle = false;
    s_area = control_rect(old >= 0 ? old : s_pressed);
    return APP_REDRAW_AREA;
}

static app_redraw_t on_key(app_ctx_t *ctx, int key) {
    if (s_usb_entry || usb_storage_active()) return stop_usb_for_navigation(ctx, key == UI_KEY_2);
    if (s_view == TRANSFER_HOME && !stop_upload_if_idle()) return APP_REDRAW_NONE;
    // 武装页：任何离页按键都先收摊，再按既有规则走。/ The arm page: any leaving
    // key closes the arm first, then follows the existing rules.
    if (s_view == TRANSFER_PUSH) {
        push_word_card_close();
        push_sink_cancel();
        if (key == UI_KEY_2) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
        s_view = TRANSFER_METHODS;
        return APP_REDRAW_PAGE;
    }
    if (key == UI_KEY_2) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
    if (s_view == TRANSFER_PASSWORD) { clear_password(); s_view = TRANSFER_NETWORKS; return APP_REDRAW_PAGE; }
    if (s_view == TRANSFER_NETWORKS) {
        if (s_wifi_settings_only || s_direct_entry) { ctx->request_return = true; return APP_REDRAW_NONE; }
        s_view = TRANSFER_METHODS; return APP_REDRAW_PAGE;
    }
    if (s_view == TRANSFER_HOME) {
        if (s_direct_entry) { ctx->request_return = true; return APP_REDRAW_NONE; }
        s_view = TRANSFER_METHODS; return APP_REDRAW_PAGE;
    }
    ctx->request_return = true;
    return APP_REDRAW_NONE;
}

static bool present(app_ctx_t* ctx, app_redraw_t redraw) {
    if (redraw != APP_REDRAW_AREA) return false;
    if (s_view == TRANSFER_PASSWORD) {
        if (s_input_settle) {
            guard_draw_result(ctx->hl, update_display_area_full_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, s_area));
            s_input_settle = false;
            return true;
        }
        guard_draw_result(ctx->hl, update_display_area_diff_with(ctx->hl,
            s_input_layout ? &E0470_WAVEFORM : &E0470_FOLLOW_WAVEFORM,
            s_input_layout ? MODE_GL16 : MODE_DU, s_area));
        return true;
    }
    guard_draw_result(ctx->hl, update_display_area_with(ctx->hl, s_settle ? &E0470_WAVEFORM : &E0470_FOLLOW_WAVEFORM,
                      s_settle ? MODE_GL16 : MODE_DU, s_area));
    s_settle = false;
    return true;
}

static bool transfer_menu_handle_enabled(app_ctx_t* ctx) {
    (void)ctx;
    return false;
}

const app_desc_t app_transfer = {
    .on_media_lost = transfer_on_media_lost,
    .title = "传书 Transfer", .detail = "热点或已有 WiFi，浏览器上传", .enter_full = false,
    .on_enter = on_enter, .on_exit = transfer_on_exit, .render = render, .present = present,
    .on_tick = on_tick, .on_gesture = on_gesture, .on_key = on_key, .owns_keys = true,
    .menu_handle_enabled = transfer_menu_handle_enabled,
};
