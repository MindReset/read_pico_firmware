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
#include "../components/read_pico_transfer/transfer_signature.h"
/* 中文：武装页 Step 4 依赖的宿主替身。app_content_open.h 与 dict_popup.h 的真身
   就在 main/apps/，quote Include 永远先中真身，替身目录遮蔽不了它们；这里按
   usb_storage_* 的同形制在测试文件里出定义与旋钮（dict_lookup.h 在 main/dict/，
   仍由 tools/transfer_ui_stubs/dict_lookup.h 遮蔽）。
   English: the host doubles the arm page's Step 4 needs. The real
   app_content_open.h and dict_popup.h live in main/apps/, so a quote include
   always hits the real ones and the stub directory cannot shadow them; the
   doubles and their knobs therefore live here, same shape as usb_storage_*
   (dict_lookup.h lives in main/dict/ and is still shadowed by the stub). */
const app_desc_t app_book = {0};
static char test_book_open_path[160];
static bool test_book_open_ok = true;
bool app_book_request_open(const char *path) {
    if (!path || !test_book_open_ok) return false;
    snprintf(test_book_open_path, sizeof(test_book_open_path), "%s", path);
    return true;
}
static char test_dict_word[64];
static bool test_dict_open_ok = true;
void dict_popup_set_anchor(int col, bool split) { (void)col; (void)split; }
bool dict_popup_open(const char *word) {
    snprintf(test_dict_word, sizeof(test_dict_word), "%s", word ? word : "");
    return test_dict_open_ok;
}
void dict_popup_reset(void) { test_dict_word[0] = 0; }
void dict_popup_draw(uint8_t *fb) { (void)fb; }
dict_popup_tap_t dict_popup_tap(uint16_t x, uint16_t y) { (void)x; (void)y; return DICT_POPUP_TAP_MISSED; }
static bool test_usb_active;
static esp_err_t test_usb_stop_error;
esp_err_t usb_storage_start(void) { test_usb_active = true; return ESP_OK; }
esp_err_t usb_storage_stop(void) {
    if (test_usb_stop_error != ESP_OK) return test_usb_stop_error;
    test_usb_active = false;
    return ESP_OK;
}
bool usb_storage_active(void) { return test_usb_active; }
bool usb_storage_connected(void) { return test_usb_active; }
static void tap(app_ctx_t* ctx, EpdRect rect) {
    ui_gesture_event_t ev = {.type = UI_GESTURE_PRESS, .x0 = rect.x + 2, .y0 = rect.y + 2, .x = rect.x + 2, .y = rect.y + 2};
    app_redraw_t pressed = on_gesture(ctx, &ev);
    // 整页刷新在 app_loop 中会取消活动手势；入口反馈必须保留松手事件。
    // app_loop cancels active gestures on a page refresh; feedback must preserve release.
    assert(pressed != APP_REDRAW_PAGE && pressed != APP_REDRAW_FULL);
    ev.type = UI_GESTURE_TAP;
    on_gesture(ctx, &ev);
}
static void qr_regression(app_ctx_t* ctx) {
    test_status = (read_pico_transfer_status_t){.mode=READ_PICO_TRANSFER_MODE_AP,.state=READ_PICO_TRANSFER_READY,.network_ready=true};
    strcpy(test_status.ssid,"ReadPico-test");
    strcpy(test_status.url,"http://192.168.4.1");
    s_mode = READ_PICO_TRANSFER_MODE_AP;
    on_enter(ctx);
    s_view = TRANSFER_HOME;
    s_qr_url = true;
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(s_qr_ready && s_qr_url && !strcmp(test_qr_payload,test_status.url));
    tap(ctx, control_rect(0));
    assert(!s_qr_url && !strcmp(test_qr_payload,test_status.ssid));
    tap(ctx, control_rect(1));
    assert(s_qr_url && !strcmp(test_qr_payload,test_status.url));
    int encodes = test_qr_encodes;
    render(ctx,ctx->fb);
    assert(test_qr_encodes == encodes);
    test_status.cur_bytes++;
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(test_qr_encodes == encodes);
    strcpy(test_status.url,"http://192.168.4.2");
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(!strcmp(test_qr_payload,test_status.url));
    test_status.network_ready=false;
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(!s_qr_ready && !test_qr_payload[0]);
    encodes=test_qr_encodes;
    render(ctx,ctx->fb);
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
    render(ctx,ctx->fb);
    assert(test_qr_encodes==encodes);
    stop_session();
    assert(!s_qr_ready && !test_qr_payload[0]);
    test_status=(read_pico_transfer_status_t){0};
    s_mode=READ_PICO_TRANSFER_MODE_AP;
    test_qr_encodes=0;
    ctx->now_ms=0;
}
// 页面标记可能因旧会话重连失败而丢失，确认遗忘仍须释放实际射频并取消排队连接。
// A failed restart can lose the page's session flag; forgetting must still stop the radio and queued reconnect.
static void wifi_recovery_regression(app_ctx_t* ctx) {
    test_configured = s_saved_configured = true;
    s_view = TRANSFER_NETWORKS;
    s_wifi_settings_only = true;
    s_session_started = false;
    s_start_pending = s_network_connect_pending = s_scan_pending = false;
    test_status = s_status = (read_pico_transfer_status_t){.mode=READ_PICO_TRANSFER_MODE_STA,
        .state=READ_PICO_TRANSFER_ERROR,.wifi_configured=true};
    test_wifi_running = true;
    int stops = test_stop_count;
    tap(ctx, network_control_rect(6));
    assert(!test_wifi_running && test_stop_count > stops && s_network_connect_pending);
    network_ui_tick(ctx);
    assert(s_session_started && !s_network_connect_pending);
    // 上传中不能遗忘；停止后可以重试。/ Forgetting waits for an active upload, then remains retryable.
    test_wifi_running = true;
    s_session_started = false;
    s_network_connect_pending = true;
    tap(ctx, network_control_rect(11));
    test_busy = true;
    stops = test_stop_count;
    int forgets = test_forget_count;
    tap(ctx, network_control_rect(11));
    assert(s_forget_confirm && test_wifi_running && test_stop_count == stops && test_forget_count == forgets);
    test_busy = false;
    tap(ctx, network_control_rect(11));
    assert(!s_forget_confirm && !test_wifi_running && !test_configured && !s_saved_configured);
    assert(!s_start_pending && !s_network_connect_pending && !s_scan_pending);
    network_ui_tick(ctx);
    assert(!s_session_started && !s_network_connect_pending);
    // 遗忘后重新选择同名网络会进入密码输入。/ Selecting the forgotten network asks for a password again.
    s_network_page = 0;
    tap(ctx, network_control_rect(0));
    assert(s_view == TRANSFER_PASSWORD && !s_password[0]);
    clear_password();
    s_wifi_settings_only = false;
    s_forget_confirm = false;
    test_status = (read_pico_transfer_status_t){0};
}
static void password_caret_regression(app_ctx_t *ctx) {
    clear_password();ui_keyboard_begin(&s_password_input,true);
    assert(s_password_input.cursor==0);
    tap(ctx,(EpdRect){24,1010,30,30}); // 数字页。/ Number panel.
    for(int i=0;i<8;i++)tap(ctx,(EpdRect){24+i*64,740,30,30});
    assert(!strcmp(s_password,"12345678")&&s_password_input.cursor==8);
    EpdRect field=password_control_rect(47);
    tap(ctx,(EpdRect){field.x+17+3*20-2,329,4,4});assert(s_password_input.cursor==3);
    tap(ctx,(EpdRect){24+64,740,30,30});assert(!strcmp(s_password,"123245678")&&s_password_input.cursor==4);
    tap(ctx,password_control_rect(45));assert(s_password_visible&&s_password_input.cursor==4);
    tap(ctx,password_control_rect(45));assert(!s_password_visible&&s_password_input.cursor==4);
    ui_text_edit_place(&s_password_input,3);
    tap(ctx,(EpdRect){600,896,30,30});assert(!strcmp(s_password,"12245678")&&s_password_input.cursor==2);
    ui_text_edit_place(&s_password_input,0);tap(ctx,(EpdRect){600,896,30,30});assert(!strcmp(s_password,"12245678"));
    clear_password();assert(!s_password[0]&&!s_password_input.cursor&&!s_password_visible);
    ui_keyboard_begin(&s_password_input,true);
    strcpy(s_password,"abcdefgh");ui_text_edit_init(&s_password_input,s_password,sizeof(s_password));
    cst836u_touch_t touch={.touched=true,.count=1,.x=610,.y=930};ctx->touch=&touch;ctx->now_ms=1000;
    ui_gesture_event_t held={.type=UI_GESTURE_PRESS,.x0=610,.y0=930,.x=610,.y=930};
    assert(on_gesture(ctx,&held)==APP_REDRAW_AREA && !s_input_layout);
    ctx->consumed=true;assert(on_tick(ctx)==APP_REDRAW_NONE);ctx->consumed=false;
    ctx->now_ms=1499;assert(on_tick(ctx)==APP_REDRAW_NONE&&!strcmp(s_password,"abcdefgh"));
    ctx->now_ms=1500;assert(on_tick(ctx)==APP_REDRAW_AREA&&!strcmp(s_password,"abcdefg"));assert(!s_input_layout);
    ctx->now_ms=1620;assert(on_tick(ctx)==APP_REDRAW_AREA&&!strcmp(s_password,"abcdef"));
    touch.count=2;ctx->now_ms=1740;assert(on_tick(ctx)==APP_REDRAW_NONE&&!strcmp(s_password,"abcdef"));
    touch.count=1;ctx->now_ms=2000;assert(on_tick(ctx)==APP_REDRAW_NONE&&!strcmp(s_password,"abcdef"));
    held.type=UI_GESTURE_PRESS;on_gesture(ctx,&held);ctx->now_ms=2500;assert(on_tick(ctx)==APP_REDRAW_AREA&&!strcmp(s_password,"abcde"));
    held.type=UI_GESTURE_CANCEL;touch.touched=false;on_gesture(ctx,&held);ctx->now_ms=2800;
    assert(on_tick(ctx)==APP_REDRAW_NONE&&!strcmp(s_password,"abcde"));ctx->touch=NULL;ctx->now_ms=0;
    clear_password();ui_keyboard_begin(&s_password_input,true);
}
// 手机推送武装页回归（Step 4）：开虚拟书、单词卡模态、活区刷新与陈事件清扫。
// The phone-push arm page regression (Step 4): the virtual book, the modal word
// card, live-region refresh and the stale-event sweep.
static void push_arm_regression(app_ctx_t *ctx) {
    // 进武装页：默认 30 分钟窗口；on_enter 把活区簿记对齐当前状态，无变化不刷。
    // Entering the arm page: the default 30-minute window; on_enter aligns the
    // live-region bookkeeping with the current state, so no change means no redraw.
    test_push_state = PUSH_STATE_WINDOW;
    snprintf(test_push_adv, sizeof(test_push_adv), "PicoPush-1a2b");
    test_push_left_ms = 30u * 60u * 1000u;
    app_transfer_request_push();
    on_enter(ctx);
    assert(s_view == TRANSFER_PUSH && test_push_armed);
    assert(test_push_arm_calls == 1 && test_push_arm_window == 30 * 60 * 1000);
    ctx->now_ms = 1000;
    assert(on_tick(ctx) == APP_REDRAW_NONE);
    // 倒计时走动 → 只重绘活区（DU 区域更新）；同值不重复刷。
    // The countdown moves - repaint only the live regions (a DU area update);
    // identical values never repaint twice.
    test_push_left_ms -= 500;
    ctx->now_ms += 500;
    assert(on_tick(ctx) == APP_REDRAW_AREA);
    assert(s_area.x == 36 && s_area.y == 168 && s_area.width == 612 && s_area.height == 496);
    ctx->now_ms += 500;
    assert(on_tick(ctx) == APP_REDRAW_NONE);
    // 推送成功：开虚拟书——标题覆盖成推送标题、清旧进度、切阅读页，且不抢全刷。
    // A push succeeds: open the virtual book - override the display title with
    // the pushed one, drop stale progress, switch to the reader, and do not
    // steal the single full refresh.
    test_push_open = true;
    snprintf(test_push_title, sizeof(test_push_title), "被推送的书");
    test_push_type = 0;
    test_book_open_path[0] = 0;
    test_book_title_path[0] = test_book_title[0] = test_progress_forget_path[0] = 0;
    ctx->request_app = NULL;
    assert(on_tick(ctx) == APP_REDRAW_NONE);
    assert(ctx->request_app == &app_book);
    assert(!strcmp(test_book_open_path, "/sdcard/.readpico/push.txt"));
    assert(!strcmp(test_book_title_path, "/sdcard/.readpico/push.txt"));
    assert(!strcmp(test_book_title, "被推送的书"));
    assert(!strcmp(test_progress_forget_path, "/sdcard/.readpico/push.txt"));
    // 开书请求失败：留页给准话，不黑屏不跳走。/ The open request fails: stay on
    // the page with a plain sentence, no black screen and no jump.
    test_book_open_ok = false;
    ctx->request_app = NULL;
    test_push_open = true;
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    assert(s_view == TRANSFER_PUSH && ctx->request_app == NULL);
    assert(!strcmp(s_push_note, "打开推送内容失败，请重试"));
    test_book_open_ok = true;
    // 单词卡命中：查词、开卡；卡片是模态的——按下不刷屏，点击问 chip（宿主恒
    // 未命中）后关卡片。/ A word-card hit: look it up and open the card; the
    // card is modal - a press never repaints, a tap asks the chip (always a
    // miss on the host) and then closes the card.
    s_push_note[0] = 0;
    test_push_word_card = true;
    snprintf(test_push_word, sizeof(test_push_word), "serendipity");
    test_dict_open_ok = true;
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    assert(s_push_word_card && !strcmp(test_dict_word, "serendipity"));
    ctx->now_ms += 500;
    assert(on_tick(ctx) == APP_REDRAW_NONE);
    ui_gesture_event_t press = {.type = UI_GESTURE_PRESS, .x0 = 300, .y0 = 700, .x = 300, .y = 700};
    assert(on_gesture(ctx, &press) == APP_REDRAW_NONE);
    press.type = UI_GESTURE_TAP;
    assert(on_gesture(ctx, &press) == APP_REDRAW_PAGE);
    assert(!s_push_word_card && !test_dict_word[0]);
    // 未收录：词典在但没这个词；词典不在：另一句准话。两者都不留白卡。
    // A miss: the dictionary is there but the word is not; no dictionary: a
    // different plain sentence. Neither leaves a blank card.
    test_dict_open_ok = false;
    test_dict_ready_flag = true;
    test_push_word_card = true;
    snprintf(test_push_word, sizeof(test_push_word), "zzzznotaword");
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    assert(!s_push_word_card && !strcmp(s_push_note, "未收录：zzzznotaword"));
    test_dict_ready_flag = false;
    test_push_word_card = true;
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    assert(!s_push_word_card && !strcmp(s_push_note, "词典不可用，无法查词"));
    // 错误码 → 用户文案；待武装失败 → 回方法页并留话。
    // Error codes to plain sentences; a failed pending arm returns to the
    // method page with a message.
    s_push_note[0] = 0;
    test_push_err = true;
    test_push_err_code = PUSH_ERR_CHECKSUM;
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    assert(!strcmp(s_push_note, "内容校验失败，请重新发送"));
    test_push_start_failed = true;
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    assert(s_view == TRANSFER_METHODS && !strcmp(s_method_message, "蓝牙启动失败，请重试"));
    // 插电常开切换：cancel + 重武装（window 0）；cancel 补的 closed 当场吃掉，
    // 页面留住，且不做内容相同的重复刷新。/ The charger-held switch: cancel and
    // re-arm (window 0); the closed posted by the cancel is swallowed on the
    // spot, the page stays, and no identical repeat refresh follows.
    app_transfer_request_push();
    on_enter(ctx);
    assert(s_view == TRANSFER_PUSH);
    test_push_state = PUSH_STATE_WINDOW;
    snprintf(test_push_adv, sizeof(test_push_adv), "PicoPush-1a2b");
    test_push_left_ms = 30u * 60u * 1000u;
    test_pmu_snapshot.charging = true;
    ctx->now_ms += 500;
    assert(on_tick(ctx) == APP_REDRAW_AREA);
    int arms = test_push_arm_calls;
    tap(ctx, control_rect(0));
    assert(test_push_arm_calls == arms + 1 && test_push_arm_window == 0);
    assert(test_push_armed && !test_push_closed && s_view == TRANSFER_PUSH);
    ctx->now_ms += 500;
    assert(on_tick(ctx) == APP_REDRAW_NONE);
    assert(s_view == TRANSFER_PUSH);
    // 未充电时常开按钮不可切。/ The hold button does nothing off the charger.
    test_pmu_snapshot.charging = false;
    arms = test_push_arm_calls;
    tap(ctx, control_rect(0));
    assert(test_push_arm_calls == arms && s_view == TRANSFER_PUSH);
    // 退出接收：cancel 之后 closed 把页面带回方法页。/ Exit receive: after the
    // cancel the closed event walks the page back to the method page.
    tap(ctx, control_rect(2));
    assert(!test_push_armed);
    assert(on_tick(ctx) == APP_REDRAW_PAGE && s_view == TRANSFER_METHODS);
    // 离页再进：nav 切走时 cancel 补的 closed 是陈事件，on_enter 清扫后页面
    // 不再被立刻扔回方法页。/ Leaving and returning: the closed posted by the
    // nav-away cancel is stale; after on_enter's sweep the page is no longer
    // thrown straight back to the method page.
    app_transfer_request_push();
    on_enter(ctx);
    assert(s_view == TRANSFER_PUSH && test_push_armed && !test_push_closed);
    ui_gesture_event_t nav = {.type = UI_GESTURE_TAP, .x0 = 20, .y0 = 1150, .x = 20, .y = 1150};
    assert(on_gesture(ctx, &nav) == APP_REDRAW_NONE);
    assert(!test_push_armed && test_push_closed);
    app_transfer_request_push();
    on_enter(ctx);
    assert(s_view == TRANSFER_PUSH && test_push_armed && !test_push_closed);
    ctx->now_ms += 500;
    assert(on_tick(ctx) == APP_REDRAW_NONE && s_view == TRANSFER_PUSH);
    // 新鲜的 closed（会话自行收官）仍然把页面放回去。/ A fresh closed (the
    // session ended on its own) still walks the page back.
    test_push_armed = false;
    test_push_closed = true;
    assert(on_tick(ctx) == APP_REDRAW_PAGE && s_view == TRANSFER_METHODS);
    // 双保险：武装标志丢了（事件漏接的极端时序）也能把页面放回去。
    // The safety net: a lost armed flag (the extreme timing of a missed event)
    // still walks the page back.
    app_transfer_request_push();
    on_enter(ctx);
    assert(s_view == TRANSFER_PUSH);
    test_push_armed = false;
    ctx->now_ms += 500;
    assert(on_tick(ctx) == APP_REDRAW_PAGE && s_view == TRANSFER_METHODS);
    transfer_on_exit(ctx);
}

int main(void) {
    assert(transfer_signature_valid("",0)&&transfer_signature_valid("中文🙂",10));
    assert(!transfer_signature_valid("a\0b",3)&&!transfer_signature_valid("\xed\xa0\x80",3));
    assert(!transfer_signature_valid("\xc0\xaf",2)&&!transfer_signature_valid("\xf4\x90\x80\x80",4));
    assert(!transfer_signature_valid("\xe4\xb8",2)&&!transfer_signature_valid("a\nb",3));
    strcpy(test_signature,"reader");char signature[96];assert(transfer_signature_get(signature,sizeof(signature))&&!strcmp(signature,"reader"));
    test_signature_fail=true;assert(transfer_signature_set("新签名")==ESP_FAIL&&!strcmp(test_signature,"reader"));
    test_signature_fail=false;assert(transfer_signature_set("新签名")==ESP_OK&&!strcmp(test_signature,"新签名"));
    assert(s_signature_revision==1);s_signature_seen=s_signature_revision;
    uint8_t fb = 0;
    app_ctx_t ctx = {.fb = &fb};
    app_transfer_request_method_picker();
    on_enter(&ctx);
    assert(s_view == TRANSFER_METHODS && !s_wifi_settings_only && !s_start_pending);
    on_tick(&ctx);
    assert(test_qr_encodes == 0);
    qr_regression(&ctx);
    test_configured = true;
    on_enter(&ctx);
    tap(&ctx, method_control_rect(0));
    assert(s_view == TRANSFER_HOME && s_start_pending && !s_scan_pending);
    assert(s_mode == READ_PICO_TRANSFER_MODE_STA);
    on_tick(&ctx);
    tap(&ctx, control_rect(2));
    assert(s_view == TRANSFER_METHODS && !s_start_pending);
    test_configured = false;
    on_enter(&ctx);
    ui_gesture_event_t cancelled = {.type=UI_GESTURE_PRESS, .x0=50, .y0=350, .x=50, .y=350};
    assert(on_gesture(&ctx, &cancelled) == APP_REDRAW_AREA);
    cancelled.type = UI_GESTURE_CANCEL;
    assert(on_gesture(&ctx, &cancelled) == APP_REDRAW_AREA);
    assert(s_view == TRANSFER_METHODS && !s_start_pending && s_pressed == -1);
    for (int i = 44; i < 48; ++i) {
        EpdRect r = password_control_rect(i);
        assert(r.x >= 0 && r.x + r.width <= UI_LOCK_WIDTH);
        assert(r.y >= 0 && r.y + r.height <= UI_LOCK_HEIGHT);
    }
    on_enter(&ctx);
    test_busy = true;
    tap(&ctx, method_control_rect(0));
    assert(s_view == TRANSFER_METHODS);
    test_busy = false;
    tap(&ctx, method_control_rect(0));
    assert(s_view == TRANSFER_NETWORKS && s_scan_pending);
    network_ui_tick(&ctx);
    assert(s_network_count == 8 && !s_scan_pending);
    test_configured = s_saved_configured = true;
    tap(&ctx, (EpdRect){540, UI_NAV_TOP + 16, 20, 20});
    assert(test_nav_index == 3);
    test_nav_index = -1;
    assert(s_view == TRANSFER_NETWORKS);
    tap(&ctx, network_control_rect(11));
    assert(test_forget_count == 0);
    tap(&ctx, network_control_rect(12));
    assert(test_forget_count == 0 && s_saved_configured && !s_forget_confirm);
    tap(&ctx, network_control_rect(11));
    test_forget_error = ESP_FAIL;
    tap(&ctx, network_control_rect(11));
    assert(test_forget_count == 1 && s_saved_configured && s_forget_confirm);
    test_forget_error = ESP_OK;
    tap(&ctx, network_control_rect(11));
    assert(test_forget_count == 2 && !s_saved_configured);
    test_configured = true;
    enter_networks();
    network_ui_tick(&ctx);
    tap(&ctx, network_control_rect(9));
    assert(s_network_page == 1);
    tap(&ctx, network_control_rect(7));
    assert(s_network_page == 0);
    // 断开保留凭据，上传期间拒绝断开，之后仍可重连。
    // Disconnect keeps credentials, refuses an active upload and allows reconnecting.
    test_status.network_ready = s_status.network_ready = true;
    s_session_started = true;
    test_busy = true;
    int stops = test_stop_count;
    tap(&ctx, network_control_rect(6));
    assert(test_stop_count == stops && s_status.network_ready);
    test_busy = false;
    tap(&ctx, network_control_rect(6));
    assert(test_stop_count > stops && !s_session_started && s_saved_configured && test_configured);
    assert(!s_start_pending && !s_network_connect_pending && test_forget_count == 2);
    test_status.network_ready = s_status.network_ready = false;
    tap(&ctx, network_control_rect(6));
    assert(s_start_pending && s_mode == READ_PICO_TRANSFER_MODE_STA);
    s_start_pending = false; s_view = TRANSFER_NETWORKS;
    tap(&ctx, network_control_rect(9));
    assert(s_network_page == 1);
    tap(&ctx, network_control_rect(3));
    assert(s_view == TRANSFER_PASSWORD && !strcmp(s_selected.ssid, "network7"));
    s_view = TRANSFER_NETWORKS; s_network_page = 0;
    tap(&ctx, network_control_rect(0));
    assert(s_view == TRANSFER_PASSWORD && !s_password[0]);
    tap(&ctx, password_control_rect(46));
    assert(test_save_count == 0 && s_view == TRANSFER_PASSWORD);
    password_caret_regression(&ctx);
    ui_gesture_event_t cancel = {.type = UI_GESTURE_PRESS,.x0 = 42,.y0 = 422,.x = 42,.y = 422};
    on_gesture(&ctx, &cancel);
    cancel.type = UI_GESTURE_TAP;
    cancel.x = 650;
    on_gesture(&ctx, &cancel);
    assert(!s_password[0]);
    // 容量及保存失败时保留密码。/ Capacity and preservation after failed saving.
    tap(&ctx,(EpdRect){24,1010,30,30});
    for(int i=0;i<70;i++)tap(&ctx,(EpdRect){24,740,30,30});
    assert(strlen(s_password)==64);
    tap(&ctx,(EpdRect){600,896,30,30});assert(strlen(s_password)==63);
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
    enter_networks();
    network_ui_tick(&ctx);
    tap(&ctx, network_control_rect(0));
    tap(&ctx,(EpdRect){24,740,30,30});
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
    assert(s_view == TRANSFER_METHODS && !ctx.request_return && ctx.request_app == NULL);
    on_enter(&ctx);
    tap(&ctx, method_control_rect(1));
    on_tick(&ctx);
    test_busy = true;
    stops_before = test_stop_count;
    tap(&ctx, control_rect(2));
    assert(test_stop_count == stops_before && s_view == TRANSFER_HOME && !ctx.request_return);
    tap(&ctx, (EpdRect){540, UI_NAV_TOP + 16, 20, 20});
    assert(test_stop_count == stops_before && test_nav_index == -1);
    test_busy = false;
    test_status.changed_count = 1;
    ctx.now_ms = 3000;
    assert(on_tick(&ctx) == APP_REDRAW_AREA);
    assert(s_status.changed_count == 1);
    s_session_started = true;
    transfer_on_exit(&ctx);
    assert(test_store_changes == 1);
    on_enter(&ctx);
    tap(&ctx, method_control_rect(1));
    on_tick(&ctx);
    s_root.is_flash = false;
    s_free = 123456;
    stops_before = test_stop_count;
    app_transfer.on_media_lost(&ctx);
    assert(test_stop_count == stops_before + 1);
    assert(s_media_lost && !s_free && !s_qr_ready && !s_root.path[0]);
    assert(!s_start_pending && !s_session_started && s_view == TRANSFER_METHODS);
    s_root.is_flash = true;
    app_transfer.on_media_lost(&ctx);
    assert(test_stop_count == stops_before + 1);
    ctx.request_return = false;
    test_configured = true;
    app_transfer_request_wifi_upload();
    on_enter(&ctx);
    assert(s_direct_entry && s_view == TRANSFER_HOME && s_start_pending);
    tap(&ctx, control_rect(2));
    assert(ctx.request_return && s_view == TRANSFER_HOME);
    ctx.request_return = false;
    app_transfer_request_hotspot_start();
    on_enter(&ctx);
    assert(s_direct_entry && s_view == TRANSFER_HOME && s_start_pending);
    tap(&ctx, control_rect(2));
    assert(ctx.request_return && s_view == TRANSFER_HOME);
    ctx.request_return = false;
    app_transfer_request_usb_start();
    on_enter(&ctx);
    assert(s_usb_start_pending && !test_usb_active);
    assert(on_tick(&ctx) == APP_REDRAW_PAGE);
    assert(s_direct_entry && s_usb_entry && s_view == TRANSFER_HOME && test_usb_active);
    test_usb_stop_error = ESP_FAIL;
    tap(&ctx, (EpdRect){36, 79, 50, 50});
    assert(test_usb_active && !ctx.request_return && s_view == TRANSFER_HOME);
    assert(on_key(&ctx, UI_KEY_2) == APP_REDRAW_PAGE && !ctx.request_app);
    assert(test_usb_active && s_usb_message[0]);
    test_usb_stop_error = ESP_OK;
    tap(&ctx, control_rect(2));
    assert(ctx.request_return && !test_usb_active);
    enter_networks();
    test_scan_count = 8; test_scan_error = ESP_OK;
    network_ui_tick(&ctx);
    wifi_recovery_regression(&ctx);
    push_arm_regression(&ctx);
    puts("transfer_ui_host_test: PASS");
}
