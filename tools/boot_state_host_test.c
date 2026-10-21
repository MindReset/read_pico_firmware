/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实启动记录跨复位、断电、损坏与 NVS 失败测试。
 * English: Exercise actual startup records across reset, power loss, corruption and NVS failures.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/boot_state.c"

static boot_record_t durable, staged;
static bool present, fail_open, fail_commit;
static unsigned commits;
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h) {
    assert(!strcmp(ns, "pico_startup") && mode == NVS_READWRITE);
    *h = 1; return fail_open ? ESP_FAIL : ESP_OK;
}
void nvs_close(nvs_handle_t h) { assert(h == 1); }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *data, size_t *size) {
    assert(h == 1 && !strcmp(key, "state") && *size == sizeof(durable));
    if (!present) return ESP_ERR_NVS_NOT_FOUND;
    memcpy(data, &durable, *size); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t size) {
    assert(h == 1 && !strcmp(key, "state") && size == sizeof(staged));
    memcpy(&staged, data, size); return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    assert(h == 1); ++commits;
    if (fail_commit) return ESP_FAIL;
    durable = staged; present = true; return ESP_OK;
}
int main(void) {
    pico_resume_t resume = {.tab=1, .reader=1, .fullscreen=1, .path="/sdcard/books/deep.epub"}, out;
    char interrupted[PICO_BOOT_PATH_MAX];
    pico_boot_init(false); assert(!pico_boot_recovery() && !pico_boot_take_resume(&out));
    pico_boot_ready();
    assert(pico_boot_save_resume(&resume) == ESP_OK);
    pico_boot_init(false);
    assert(!pico_boot_recovery() && pico_boot_take_resume(&out));
    assert(out.reader && out.fullscreen && !strcmp(out.path, resume.path));
    assert(!pico_boot_take_resume(&out) && !durable.resume_valid);
    // 恢复过程中又复位，入口已消费且启动未完成，回到保护模式。
    // Reset again while resuming: consumption and incomplete startup prevent a loop.
    pico_boot_init(false); assert(pico_boot_recovery() && !pico_boot_take_resume(&out));
    pico_boot_ready(); pico_boot_init(false); assert(!pico_boot_recovery()); pico_boot_ready();
    // 密码取消/深睡再次开机不能丢失恢复，但实际开书前必须消费。
    // PIN cancellation and another deep boot retain resume; actual opening still consumes it.
    assert(pico_boot_save_resume(&resume) == ESP_OK); pico_boot_init(false); pico_boot_ready();
    pico_boot_hold_resume(); assert(durable.resume_valid);
    pico_boot_init(false); assert(!pico_boot_recovery()); pico_boot_ready(); pico_boot_hold_resume();
    assert(pico_boot_take_resume(&out) && !strcmp(out.path, resume.path) && !durable.resume_valid);
    assert(pico_boot_save_resume(&resume) == ESP_OK); pico_boot_init(false); pico_boot_ready(); pico_boot_hold_resume();
    fail_commit=true; assert(!pico_boot_take_resume(&out)); fail_commit=false; pico_boot_clear_resume();
    assert(pico_boot_book_begin("/sdcard/books/large.epub") == ESP_OK);
    pico_boot_init(false); assert(pico_boot_recovery());
    assert(pico_boot_interrupted_book(interrupted, sizeof(interrupted)) && !strcmp(interrupted, "/sdcard/books/large.epub"));
    assert(!pico_boot_interrupted_book(interrupted, 8));
    pico_boot_ready(); pico_boot_init(false); pico_boot_ready();
    assert(!pico_boot_recovery() && !pico_boot_asset_allowed("/sdcard/books/large.epub") && pico_boot_asset_allowed(resume.path));
    // 密码取消/深睡再次开机不能丢失恢复，但实际开书前必须消费。
    // PIN cancellation and another deep boot retain resume; actual opening still consumes it.
    assert(pico_boot_save_resume(&resume) == ESP_OK); pico_boot_init(false); pico_boot_ready();
    pico_boot_hold_resume(); assert(durable.resume_valid);
    pico_boot_init(false); assert(!pico_boot_recovery()); pico_boot_ready(); pico_boot_hold_resume();
    assert(pico_boot_take_resume(&out) && !strcmp(out.path, resume.path) && !durable.resume_valid);
    assert(pico_boot_save_resume(&resume) == ESP_OK); pico_boot_init(false); pico_boot_ready(); pico_boot_hold_resume();
    fail_commit=true; assert(!pico_boot_take_resume(&out)); fail_commit=false; pico_boot_clear_resume();
    assert(pico_boot_book_begin("/sdcard/books/large.epub") == ESP_OK); pico_boot_book_end(true);
    assert(pico_boot_asset_allowed("/sdcard/books/large.epub"));
    assert(pico_boot_save_resume(&resume) == ESP_OK); pico_boot_clear_resume();
    pico_boot_init(false); assert(!pico_boot_take_resume(&out)); pico_boot_ready();
    assert(pico_boot_save_resume(&resume) == ESP_OK); pico_boot_init(true);
    assert(pico_boot_recovery() && !pico_boot_take_resume(&out) && !pico_boot_interrupted_book(interrupted, sizeof(interrupted)));
    pico_boot_ready();
    resume.path[0]=0; assert(pico_boot_save_resume(&resume)==ESP_ERR_INVALID_ARG);
    strcpy(resume.path, "/sdcard/../danger.epub"); assert(pico_boot_save_resume(&resume)==ESP_ERR_INVALID_ARG);
    assert(pico_boot_book_begin("/sdcard/../danger.epub")==ESP_ERR_INVALID_ARG);
    assert(pico_boot_book_begin("/other/book.epub")==ESP_ERR_INVALID_ARG);
    durable.checksum ^= 1;
    pico_boot_init(false); assert(pico_boot_recovery()); pico_boot_ready();
    fail_commit=true;
    assert(pico_boot_book_begin("/sdcard/book.epub")==ESP_FAIL);
    pico_boot_init(false); assert(pico_boot_recovery() && !pico_boot_take_resume(&out));
    fail_commit=false; fail_open=true;
    pico_boot_init(false); assert(pico_boot_recovery() && pico_boot_book_begin("/sdcard/book.epub")!=ESP_OK);
    fail_open=false;
    assert(commits > 0);
    puts("PASS: one-shot reader/fullscreen resume; power-cut book quarantine; explicit retry; no false book blame; absent/corrupt NVS and commit failure; checked paths");
}
