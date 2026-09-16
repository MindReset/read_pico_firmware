/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 概览页。I2C 普查、识别码、电池、构建时间。
 *
 * Overview page: I2C census, identity codes, battery, and build time.
 */

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "read_pico_board.h"
#include "read_pico_pmu.h"
#include "read_pico_sd.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_menu.h"

#define HOME_TITLE "概览 Overview"
#define HOME_ABOUT_ZH \
    "此设备为墨水屏开发板。你可以自行编写固件，也可以使用社区生态已适配的开源固件。"
#define HOME_ABOUT_EN \
    "This is an EPD development set. \nYou can write your own firmware, or use community open-source firmware already ported to it."
#define HOME_ABOUT_PX UI_PX_BODY
#define HOME_ABOUT_LH (HOME_ABOUT_PX + 10)
#define HOME_MISS_PX UI_PX_BODY
#define HOME_MISS_LH (HOME_MISS_PX + 8)
#define HOME_ST_PAD 8

enum {
    HOME_DRAW_PAGE = 0,
    HOME_DRAW_NO_MARK,
    HOME_DRAW_MARK,
};

typedef struct {
    char sub[64];
    char payload[80];
    bool pass;
    EpdRect dm;
    ui_header_skel_t head;
    const char* miss[READ_PICO_I2C_DEV_N];
    int miss_n;
} home_layout_t;

static int s_draw = HOME_DRAW_PAGE;
static EpdRect s_dm_rect;
static bool s_have_dm;

static const char* charge_text(const pmu_snapshot_t* s) {
    if (!s->status_ok) return "未读到 No data";
    switch (s->charge_state) {
        case PMU_CHARGE_NOT_CHARGING: return "未充电 Idle";
        case PMU_CHARGE_CHARGING: return "充电中 Charging";
        case PMU_CHARGE_FULL_INFERRED: return "已满 Full";
        case PMU_CHARGE_FAULT: return "故障 Fault";
        default: return "未知 Unknown";
    }
}

static void fmt_battery(char* buf, size_t n, const pmu_snapshot_t* s) {
    if (!s->status_ok) {
        snprintf(buf, n, "未读到 No data");
        return;
    }
    snprintf(buf, n, "%u.%03u V　%u%%",
             s->battery_mv / 1000, s->battery_mv % 1000, s->soc_permille / 10);
}

static void fmt_mac_code(char* code, size_t n) {
    uint8_t mac[6] = { 0 };
    if (esp_read_mac(mac, ESP_MAC_BASE) != ESP_OK) {
        snprintf(code, n, "NA");
        return;
    }
    snprintf(
        code, n, "%02X%02X%02X%02X%02X%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    );
}

static void fmt_uid_code(char* code, size_t n) {
    uint8_t uid[PMU_CHIP_UID_LEN];
    if (read_pico_pmu_uid_get(uid) != ESP_OK) {
        snprintf(code, n, "NA");
        return;
    }
    size_t o = 0;
    for (int i = 0; i < PMU_CHIP_UID_LEN && o + 2 < n; i++) {
        o += (size_t)snprintf(code + o, n - o, "%02X", uid[i]);
    }
}

static void fill_census(home_layout_t* L) {
    const read_pico_i2c_census_t* c = read_pico_i2c_census();
    L->pass = c->all_online;
    L->miss_n = 0;
    for (int i = 0; i < READ_PICO_I2C_DEV_N; i++) {
        if (c->dev[i].online || c->dev[i].name == NULL) continue;
        L->miss[L->miss_n++] = c->dev[i].name;
    }
}

static EpdRect miss_slot(const home_layout_t* L) {
    int w = ttf_text_width_px(HOME_MISS_PX, "不在线");
    for (int i = 0; i < L->miss_n; i++) {
        const int nw = ttf_text_width_px(HOME_MISS_PX, L->miss[i]);
        if (nw > w) w = nw;
    }
    const int h = HOME_MISS_LH * (1 + L->miss_n);
    return (EpdRect){
        .x = UI_BAR_MARGIN,
        .y = UI_LOCK_HEIGHT - UI_BAR_MARGIN - h,
        .width = w,
        .height = h,
    };
}

static int home_st_side(void) {
    int above = 0;
    int below = 0;
    ttf_measure_line_px(UI_PX_TITLE, "P", &above, &below);
    const int box = ttf_text_width_px(UI_PX_TITLE, "P");
    const int h = above + below;
    const int side = (h > box ? h : box) + 2 * HOME_ST_PAD;
    return side < 8 ? UI_PX_TITLE : side;
}

static home_layout_t home_layout(void) {
    home_layout_t L = { 0 };
    snprintf(L.sub, sizeof(L.sub), "%s / %s", READ_PICO_DEVICE_NAME, READ_PICO_PRODUCT_NAME);
    char mac[24];
    char uid[32];
    fmt_mac_code(mac, sizeof(mac));
    fmt_uid_code(uid, sizeof(uid));
    fill_census(&L);
    snprintf(L.payload, sizeof(L.payload), "SN=%s|ID=%s|R=%s", mac, uid,
             L.pass ? "PASS" : "FAIL");
    if (L.pass) {
        const int s = ui_datamatrix_side(L.payload);
        if (s >= 8) {
            L.dm = (EpdRect){
                .x = UI_BAR_MARGIN,
                .y = UI_LOCK_HEIGHT - UI_BAR_MARGIN - s,
                .width = s,
                .height = s,
            };
        }
    } else {
        L.dm = miss_slot(&L);
    }
    L.head = ui_header_skel(HOME_TITLE, L.sub, home_st_side());
    return L;
}

static const char* utf8_next(const char* s) {
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) return s + 1;
    if (c < 0xE0) return s + 2;
    if (c < 0xF0) return s + 3;
    return s + 4;
}

static const char* utf8_prev(const char* start, const char* cur) {
    const char* p = start;
    const char* prev = start;
    while (p < cur) {
        prev = p;
        p = utf8_next(p);
    }
    return prev;
}

static bool ascii_word(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
        || (c >= '0' && c <= '9');
}

static bool text_fits(const char* buf, int max_w) {
    return ttf_text_width_px(HOME_ABOUT_PX, buf) <= max_w;
}

// 行带 = 内容宽减去与 Data Matrix 在 y 上重叠的左下角方块。/ Lane width = content width minus the bottom-left square that overlaps this line in y.
static void about_lane(int y, EpdRect dm, int* x, int* max_w) {
    int x0 = UI_MARGIN;
    const int x1 = ui_content_right();
    if (dm.width >= 8 && y < dm.y + dm.height && y + HOME_ABOUT_LH > dm.y) {
        const int cut = dm.x + dm.width + UI_GAP;
        if (cut > x0) x0 = cut;
    }
    *x = x0;
    *max_w = x1 - x0;
}

static const char* about_fit(
    const char* p, int max_w, bool* hyphen, char* buf, size_t buf_n
) {
    *hyphen = false;
    const char* end = p;
    const char* q = p;
    while (*q != '\0' && *q != '\n') {
        const char* next = utf8_next(q);
        size_t n = (size_t)(next - p);
        if (n >= buf_n) break;
        memcpy(buf, p, n);
        buf[n] = '\0';
        if (!text_fits(buf, max_w) && end != p) break;
        end = next;
        q = next;
    }
    if (end == p) return *p != '\0' && *p != '\n' ? utf8_next(p) : p;
    while (end > p && end[-1] == ' ') end--;

    if (*end != '\0' && *end != '\n' && end > p
        && ascii_word((unsigned char)end[-1])
        && ascii_word((unsigned char)*end)) {
        *hyphen = true;
        while (end > p) {
            size_t n = (size_t)(end - p);
            if (n + 2 > buf_n) {
                end = utf8_prev(p, end);
                continue;
            }
            memcpy(buf, p, n);
            buf[n] = '-';
            buf[n + 1] = '\0';
            if (text_fits(buf, max_w)) break;
            const char* prev = utf8_prev(p, end);
            if (prev == end) break;
            end = prev;
        }
    }
    return end;
}

static int draw_about_para(uint8_t* fb, int y, const char* text, EpdRect dm) {
    const char* p = text;
    char line[128];
    while (*p != '\0') {
        if (*p == '\n') {
            y += HOME_ABOUT_LH;
            p++;
            continue;
        }
        while (*p == ' ') p++;
        if (*p == '\0' || *p == '\n') continue;

        int x = 0;
        int max_w = 0;
        about_lane(y, dm, &x, &max_w);
        if (max_w < 40) break;

        bool hyphen = false;
        const char* end = about_fit(p, max_w, &hyphen, line, sizeof(line));
        size_t n = (size_t)(end - p);
        if (n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, p, n);
        if (hyphen && n + 1 < sizeof(line)) {
            line[n] = '-';
            line[n + 1] = '\0';
        } else {
            line[n] = '\0';
        }
        ui_text(fb, x, y, HOME_ABOUT_PX, line, EPD_DRAW_ALIGN_LEFT, false);
        y += HOME_ABOUT_LH;
        p = end;
        if (*p == ' ') p++;
        if (*p == '\n') p++;
    }
    return y;
}

static void draw_header_mark(uint8_t* fb, const home_layout_t* L) {
    ui_draw_header_skel(fb, &L->head, HOME_TITLE, L->sub);
    const EpdRect acc = L->head.accessory;
    if (acc.width < 8) return;
    const int cx = acc.x + acc.width / 2;
    const int cy = acc.y + acc.height / 2;
    if (L->pass) {
        ui_text_vc(fb, cx, cy, UI_PX_TITLE, "P", EPD_DRAW_ALIGN_CENTER, false);
        return;
    }
    epd_fill_rect(acc, UI_GRAY_BLACK, fb);
    ui_text_vc(fb, cx, cy, UI_PX_TITLE, "P", EPD_DRAW_ALIGN_CENTER, true);
}

static void draw_offline(uint8_t* fb, const home_layout_t* L) {
    if (L->dm.width < 8) return;
    int y = L->dm.y;
    ui_text(fb, L->dm.x, y, HOME_MISS_PX, "不在线", EPD_DRAW_ALIGN_LEFT, false);
    y += HOME_MISS_LH;
    for (int i = 0; i < L->miss_n; i++) {
        ui_text(fb, L->dm.x, y, HOME_MISS_PX, L->miss[i], EPD_DRAW_ALIGN_LEFT, false);
        y += HOME_MISS_LH;
    }
}

static void draw_status_page(uint8_t* fb) {
    const home_layout_t L = home_layout();
    s_dm_rect = L.dm;
    s_have_dm = L.pass && L.dm.width >= 8;

    if (s_draw != HOME_DRAW_MARK) {
        char line[64];
        const pmu_snapshot_t* pmu = read_pico_pmu_get();
        const esp_app_desc_t* desc = esp_app_get_description();
        draw_header_mark(fb, &L);

        int y = UI_CONTENT_TOP;
        y = ui_draw_section(fb, y, "供电 Power");
        fmt_battery(line, sizeof(line), pmu);
        y = ui_draw_row(fb, y, "电池 Battery", line);
        y = ui_draw_row(fb, y, "充电 Charge", charge_text(pmu));

        y = ui_draw_section(fb, y + UI_SECTION_GAP, "固件 Firmware");
        snprintf(line, sizeof(line), "%s", desc->version);
        y = ui_draw_row(fb, y, "主机 Host", line);
        snprintf(line, sizeof(line), "%s %s", desc->date, desc->time);
        y = ui_draw_row(fb, y, "构建 Build", line);
        if (pmu->identity_ok) {
            snprintf(
                line, sizeof(line), "%u.%u.%u",
                pmu->fw_major, pmu->fw_minor, pmu->fw_patch
            );
            y = ui_draw_row(fb, y, "PMU 版本", line);
            snprintf(line, sizeof(line), "%u", pmu->hw_rev);
            y = ui_draw_row(fb, y, "硬件 Hardware", line);
        } else {
            y = ui_draw_row(fb, y, "PMU 版本", "未连上 Offline");
            y = ui_draw_row(fb, y, "硬件 Hardware", "未连上 Offline");
        }

        y = ui_draw_section(fb, y + UI_SECTION_GAP, "说明 About");
        y = draw_about_para(fb, y, HOME_ABOUT_ZH, L.dm);
        draw_about_para(fb, y + 8, HOME_ABOUT_EN, L.dm);
        if (!L.pass) draw_offline(fb, &L);
    }

    if (s_draw != HOME_DRAW_NO_MARK && s_have_dm) {
        ui_draw_datamatrix(fb, L.dm, L.payload);
    }
}

static void render(app_ctx_t* ctx, uint8_t* fb) {
    if (!ttf_font_ready()) {
        read_pico_sd_info_t sd = { 0 };
        read_pico_sd_get_info(&sd);
        ui_draw_no_font_page(fb, sd.present, false);
        return;
    }

    if (s_draw == HOME_DRAW_MARK) {
        draw_status_page(fb);
        return;
    }

    ui_clear_page(fb);
    draw_status_page(fb);
    ui_draw_menu_handle(fb, false);
}

static bool home_present(app_ctx_t* ctx, app_redraw_t redraw) {
    if (redraw == APP_REDRAW_NONE || redraw == APP_REDRAW_DONE) return true;

    const bool force_full = (redraw == APP_REDRAW_FULL) && APP_PAGE_FORCE_FULL;
    s_draw = ttf_font_ready() ? HOME_DRAW_NO_MARK : HOME_DRAW_PAGE;
    render(ctx, ctx->fb);
    enum EpdDrawError err = force_full
        ? update_display_full(ctx->hl)
        : update_display_mode(ctx->hl, APP_PAGE_REFRESH_MODE);
    guard_draw_result(ctx->hl, err);

    if (s_draw != HOME_DRAW_NO_MARK) {
        s_draw = HOME_DRAW_PAGE;
        return true;
    }

    s_draw = HOME_DRAW_MARK;
    draw_status_page(ctx->fb);
    s_draw = HOME_DRAW_PAGE;
    if (s_have_dm) {
        err = update_display_area_with(
            ctx->hl, &E0470_WAVEFORM, MODE_DU, s_dm_rect
        );
        guard_draw_result(ctx->hl, err);
    }
    return true;
}

static void on_enter(app_ctx_t* ctx) {
    ctx->leaf = 0;
    read_pico_pmu_refresh();
    (void)read_pico_pmu_uid_get(NULL);
}

const app_desc_t app_home = {
    .title = "概览 Overview",
    .detail = "设备识别码与状态 Device Info & Status",
    .enter_full = true,
    .render = render,
    .present = home_present,
    .on_enter = on_enter,
};
