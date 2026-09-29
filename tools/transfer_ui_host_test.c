/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：直接验证触屏配网状态机及输入边界。
 * English: Exercise the device provisioning state machine and input bounds.
 * 冻结：仅供宿主测试，不访问网络。/ Frozen: Host tests only; no network access.
 */
#include "transfer_ui_test_env.h"
#include "../main/apps/app_transfer.c"
#include <assert.h>
static void tap(app_ctx_t* ctx, EpdRect rect) {
    ui_gesture_event_t ev = {.type = UI_GESTURE_PRESS, .x0 = rect.x + 2, .y0 = rect.y + 2, .x = rect.x + 2, .y = rect.y + 2};
    on_gesture(ctx, &ev);
    ev.type = UI_GESTURE_TAP;
    on_gesture(ctx, &ev);
}
static void qr_regression(app_ctx_t* ctx) {
    test_status = (read_pico_transfer_status_t){.mode=READ_PICO_TRANSFER_MODE_AP,.state=READ_PICO_TRANSFER_READY,.network_ready=true};
    strcpy(test_status.ssid,"ReadPico-test");
    strcpy(test_status.url,"http://192.168.4.1");
    s_mode = READ_PICO_TRANSFER_MODE_AP;
    on_enter(ctx);
    on_tick(ctx);
    assert(s_qr_ready && !s_qr_url && !strcmp(test_qr_payload,"ReadPico-test"));
    int encodes = test_qr_encodes;
    render(ctx,ctx->fb);
    assert(test_qr_encodes == encodes);
    test_status.cur_bytes++;
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(test_qr_encodes == encodes);
    EpdRect toggle = control_rect(3);
    assert(toggle.x+toggle.width < 404 && toggle.y+toggle.height <= 518);
    ui_gesture_event_t ev={.type=UI_GESTURE_PRESS,.x0=toggle.x+2,.y0=toggle.y+2,.x=toggle.x+2,.y=toggle.y+2};
    on_gesture(ctx,&ev);
    ev.type=UI_GESTURE_TAP; ev.x=650;
    on_gesture(ctx,&ev);
    assert(!s_qr_url && test_qr_encodes==encodes);
    tap(ctx,toggle);
    assert(s_qr_url && s_qr_ready && !strcmp(test_qr_payload,test_status.url));
    strcpy(test_status.url,"http://192.168.4.2");
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(!strcmp(test_qr_payload,test_status.url));
    test_status.network_ready=false;
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(!s_qr_ready && !test_qr_payload[0]);
    encodes=test_qr_encodes;
    tap(ctx,toggle);
    assert(test_qr_encodes==encodes);
    test_status.network_ready=true;
    test_qr_failure=true;
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(!s_qr_ready);
    test_qr_failure=false;
    queue_network_start(READ_PICO_TRANSFER_MODE_STA);
    assert(!s_qr_ready && !test_qr_payload[0]);
    test_status.mode=READ_PICO_TRANSFER_MODE_STA;
    strcpy(test_status.url,"http://10.20.30.40");
    on_tick(ctx);
    assert(s_qr_url && s_qr_ready && !strcmp(test_qr_payload,test_status.url));
    encodes=test_qr_encodes;
    tap(ctx,toggle);
    render(ctx,ctx->fb);
    assert(test_qr_encodes==encodes);
    stop_session();
    assert(!s_qr_ready && !test_qr_payload[0]);
    test_status=(read_pico_transfer_status_t){0};
    s_mode=READ_PICO_TRANSFER_MODE_AP;
    test_qr_encodes=0;
    ctx->now_ms=0;
}
int main(void) {
    uint8_t fb = 0;
    app_ctx_t ctx = {.fb = &fb};
    on_enter(&ctx);
    on_tick(&ctx);
    assert(test_qr_encodes == 0);
    qr_regression(&ctx);
    for (int i = 0; i < 47; ++i) {
        EpdRect r = password_control_rect(i);
        assert(r.x >= 0 && r.x + r.width <= UI_LOCK_WIDTH);
        assert(r.y >= 0 && r.y + r.height <= UI_LOCK_HEIGHT);
    }
    on_enter(&ctx);
    test_busy = true;
    tap(&ctx, control_rect(1));
    assert(s_view == TRANSFER_HOME);
    test_busy = false;
    tap(&ctx, control_rect(1));
    assert(s_view == TRANSFER_NETWORKS && s_scan_pending);
    network_ui_tick(&ctx);
    assert(s_network_count == 8 && !s_scan_pending);
    tap(&ctx, ui_row_rect(1, 2, 176, 84));
    assert(test_forget_count == 0);
    tap(&ctx, ui_row_rect(1, 2, 600, UI_BTN_H));
    assert(test_forget_count == 0 && s_saved_configured && !s_forget_confirm);
    tap(&ctx, ui_row_rect(1, 2, 176, 84));
    test_forget_error = ESP_FAIL;
    tap(&ctx, ui_row_rect(0, 2, 600, UI_BTN_H));
    assert(test_forget_count == 1 && s_saved_configured && s_forget_confirm);
    test_forget_error = ESP_OK;
    tap(&ctx, ui_row_rect(0, 2, 600, UI_BTN_H));
    assert(test_forget_count == 2 && !s_saved_configured);
    test_configured = true;
    enter_networks();
    network_ui_tick(&ctx);
    tap(&ctx, network_control_rect(9));
    assert(s_network_page == 1);
    tap(&ctx, network_control_rect(7));
    assert(s_network_page == 0);
    tap(&ctx, network_control_rect(0));
    assert(s_view == TRANSFER_PASSWORD && !s_password[0]);
    tap(&ctx, password_control_rect(46));
    assert(test_save_count == 0 && s_view == TRANSFER_PASSWORD);
    ui_gesture_event_t cancel = {.type = UI_GESTURE_PRESS,.x0 = 42,.y0 = 422,.x = 42,.y = 422};
    on_gesture(&ctx, &cancel);
    cancel.type = UI_GESTURE_TAP;
    cancel.x = 650;
    on_gesture(&ctx, &cancel);
    assert(!s_password[0]);
    bool reachable[128] = {0};
    for (int mode = 0; mode < 3; ++mode) {
        s_keyboard_mode = mode;
        for (int i = 0; i < 40; ++i) reachable[(unsigned char)keyboard_chars()[i]] = true;
    }
    reachable[' '] = true;
    for (int c = 32; c < 127; ++c) assert(reachable[c]);
    s_keyboard_mode = 0;
    for (int i = 0; i < 70; ++i) tap(&ctx, password_control_rect(0));
    assert(strlen(s_password) == 64);
    tap(&ctx, password_control_rect(43));
    assert(strlen(s_password) == 63);
    tap(&ctx, password_control_rect(45));
    assert(s_password_visible);
    test_save_error = ESP_FAIL;
    tap(&ctx, password_control_rect(46));
    assert(s_view == TRANSFER_PASSWORD && strlen(s_password) == 63);
    test_save_error = ESP_OK;
    tap(&ctx, password_control_rect(46));
    assert(s_view == TRANSFER_HOME && s_mode == READ_PICO_TRANSFER_MODE_STA && s_start_pending);
    assert(!s_password[0]);
    assert(strcmp(test_saved_ssid, "中文家庭网络") == 0);
    tap(&ctx, control_rect(1));
    network_ui_tick(&ctx);
    tap(&ctx, network_control_rect(0));
    tap(&ctx, password_control_rect(0));
    tap(&ctx, password_control_rect(44));
    assert(s_view == TRANSFER_NETWORKS && !s_password[0]);
    test_scan_count = 0;
    tap(&ctx, network_control_rect(8));
    network_ui_tick(&ctx);
    assert(!s_network_count && s_network_message[0]);
    test_scan_error = ESP_FAIL;
    tap(&ctx, network_control_rect(8));
    network_ui_tick(&ctx);
    assert(!s_network_count && s_network_message[0]);
    tap(&ctx, network_control_rect(6));
    assert(s_view == TRANSFER_HOME && s_start_pending && s_mode == READ_PICO_TRANSFER_MODE_STA);
    int stops_before = test_stop_count;
    tap(&ctx, control_rect(2));
    assert(test_stop_count == stops_before + 1 && !s_start_pending);
    assert(ctx.request_return && ctx.request_app == NULL);
    on_enter(&ctx);
    on_tick(&ctx);
    test_status.changed_count = 1;
    ctx.now_ms = 3000;
    assert(on_tick(&ctx) == APP_REDRAW_FULL);
    assert(s_status.changed_count == 1);
    transfer_on_exit(&ctx);
    assert(!test_font_suspended);
    assert(test_store_changes == 1);
    on_enter(&ctx);
    on_tick(&ctx);
    s_root.is_flash = false;
    s_free = 123456;
    stops_before = test_stop_count;
    app_transfer.on_media_lost(&ctx);
    assert(test_font_suspended);
    assert(test_stop_count == stops_before + 1);
    assert(s_media_lost && !s_free && !s_qr_ready && !s_root.path[0]);
    assert(!s_start_pending && !s_session_started && s_view == TRANSFER_HOME);
    s_root.is_flash = true;
    app_transfer.on_media_lost(&ctx);
    assert(test_stop_count == stops_before + 1);
    puts("transfer_ui_host_test: PASS");
}
