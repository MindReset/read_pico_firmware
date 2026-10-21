#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：执行真实官方移植的 IOE/外设收口与主机下电，验证唤醒方向及失败恢复。
# English: Exercise actual ported IOE/peripheral parking and host power-down, including wake directions and failed handoffs.
from pathlib import Path
import re
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
board = ROOT / 'components/read_pico/read_pico_board.c'
sleep = ROOT / 'main/sleep.c'
text = board.read_text()
defines = '\n'.join(line for line in text.splitlines() if re.match(
    r'#define (IOE_MODE|IOE_CW_INT|IOE_CONFIG_PORT0|IOE_CONFIG_PORT0_HOST_OFF|IOE_CONFIG_PORT1_SLEEP|IOE_CONFIG_PORT1|SD_CLK_GPIO|D\d+|EPD_XCL|EPD_CKV|EPD_XLE|EPD_XSTL|EPD_SPV)\b', line))
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef int esp_err_t;
typedef int gpio_num_t;
typedef void *cst836u_handle_t;
typedef void *sc7a20h_handle_t;
typedef int app_sleep_mode_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_INVALID_ARG -3
#define ESP_ERR_TIMEOUT -4
#define APP_SLEEP_LIGHT 0
#define APP_SLEEP_DEEP 1
#define APP_SLEEP_OFF 2
#define GPIO_MODE_INPUT 1
#define GPIO_PULLDOWN_ONLY 2
#define CST836U_MODE_NORMAL 0
#define CST836U_MODE_DEEPSLEEP 3
#define HOST_EN_DROP_WAIT_MS 5000
#define pdMS_TO_TICKS(n) (n)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static void *s_ioe = (void *)1;
static uint8_t ioe_output;
static bool rails_on, s_mode_parked;
static cst836u_handle_t s_tp;
static sc7a20h_handle_t s_acc;
static bool s_acc_was_on;
static uint8_t config[2], output[2];
static int step, fault, clears;
static int pins[49], pin_direction[49], pin_pull[49];
static bool buzzer, acc_on, transfer_ok = true, ota;
static bool avatar_ok=true;
static bool book_cover_jobs_quiesce(unsigned ms){assert(ms==2000);return true;}
static bool ui_avatar_quiesce(unsigned ms){assert(ms==2000);return avatar_ok;}
static int tp_mode, touch_wakes, acc_ups, acc_downs, cpu_requests;
static int off_calls, deep_calls, pauses, resumes, syncs, prepared, radio_stops;
static int error_result, waits;
static esp_err_t operation(void) { return ++step == fault ? ESP_FAIL : ESP_OK; }
static esp_err_t fca9555_set_output(void *h, int p, uint8_t v) {
    assert(h == s_ioe); esp_err_t e = operation(); if (!e) output[p] = v; return e;
}
static esp_err_t fca9555_set_config(void *h, int p, uint8_t v) {
    assert(h == s_ioe); esp_err_t e = operation(); if (!e) config[p] = v; return e;
}
static esp_err_t fca9555_clear_int(void *h) { assert(h == s_ioe); ++clears; return ESP_OK; }
static esp_err_t ioe_commit(void) { return fca9555_set_output(s_ioe, 0, ioe_output); }
static void gpio_sleep_set_direction(int p, int d) { assert(p >= 0 && p < 49); pin_direction[p] = d; }
static void gpio_sleep_set_pull_mode(int p, int m) { pin_pull[p] = m; }
static void gpio_sleep_sel_en(int p) { ++pins[p]; }
static bool read_pico_buzzer_score_busy(void) { return buzzer; }
static int read_pico_buzzer_score_stop(void) { buzzer = false; return ESP_OK; }
static bool sc7a20h_powered(void *h) { return h && acc_on; }
static int sc7a20h_power_down(void *h) { assert(h == s_acc); acc_on = false; ++acc_downs; return ESP_OK; }
static int sc7a20h_power_up(void *h) { assert(h == s_acc); acc_on = true; ++acc_ups; return ESP_OK; }
static int cst836u_get_mode(void *h) { assert(h == s_tp); return tp_mode; }
static int cst836u_set_mode(void *h, int m) { assert(h == s_tp); tp_mode = m; return ESP_OK; }
static int cst836u_wake(void *h) { assert(h == s_tp); assert(config[0] == 0x64 && config[1] == 0xff); tp_mode = CST836U_MODE_NORMAL; ++touch_wakes; return ESP_OK; }
static int sleep_cpu_configure(bool enabled) { assert(enabled); ++cpu_requests; return ESP_OK; }
static bool pico_online_busy(void) { return ota; }
static bool read_pico_transfer_pause_for_sleep(void) { ++pauses; return transfer_ok; }
static void read_pico_transfer_resume_after_sleep(void) { ++resumes; }
static int ble_pt_stop(int timeout) { assert(timeout == 2000); ++radio_stops; return ESP_OK; }
static int app_sync_time_to_pmu(void) { ++syncs; return ESP_OK; }
static void pmu_selftest_prepare_powerdown(void) { ++prepared; rails_on = false; }
static int pmu_result(void) {
    assert(prepared && !rails_on && !acc_on && tp_mode == CST836U_MODE_DEEPSLEEP);
    assert(config[0] == 0x60 && config[1] == 0 && output[1] == 0xff);
    assert(!(output[0] & 5));
    return error_result;
}
static int read_pico_pmu_power_off(void) { ++off_calls; return pmu_result(); }
static int read_pico_pmu_report_sleep(void) { ++deep_calls; return pmu_result(); }
static void vTaskDelay(int ms) { waits += ms; }
'''
for i in range(49):
    unit += f'\n#define GPIO_NUM_{i} {i}\n'
unit += defines + '\n'
for name in ('sleep_pulldown_pins', 'read_pico_ioe_sleep', 'read_pico_ioe_wake'):
    unit += function(name, board) + '\n'
for name in ('app_sleep_bind', 'periph_suspend', 'periph_resume', 'app_enter_host_sleep'):
    unit += function(name, sleep) + '\n'
unit += r'''
static void reset(void) {
    s_ioe = (void *)1; step = fault = clears = 0; config[0] = 0x64; config[1] = 0xff;
    ioe_output = output[0] = 0x81; output[1] = 0; rails_on = false; s_mode_parked = false;
    buzzer = acc_on = true; s_acc_was_on = false; tp_mode = CST836U_MODE_NORMAL;
    touch_wakes = acc_ups = acc_downs = off_calls = deep_calls = pauses = resumes = 0;
    syncs = prepared = radio_stops = waits = 0; error_result = ESP_FAIL; transfer_ok = true; ota = false;
}
int main(void) {
    reset(); app_sleep_bind((void *)2, (void *)3); assert(cpu_requests == 1);
    sleep_pulldown_pins();
    int count = 0;
    for (int p = 0; p < 49; ++p) if (pins[p]) {
        assert(pins[p] == 1 && pin_direction[p] == GPIO_MODE_INPUT && pin_pull[p] == GPIO_PULLDOWN_ONLY); ++count;
    }
    assert(count == 22 && pins[38] && !pins[39] && !pins[40] && !pins[41] && !pins[42] && !pins[43] && !pins[44]);
    // 浅睡保留 CW_INT 输入，关机才转输出低；恢复后触摸可唤醒，运行前已恢复 IOE。
    // Light sleep retains CW_INT input; only host-off drives it low. Restore IOE before touch reset.
    for (int off = 0; off < 2; ++off) {
        reset(); periph_suspend(off);
        assert(!buzzer && !acc_on && tp_mode == CST836U_MODE_DEEPSLEEP);
        assert(config[0] == (off ? 0x60 : 0x64) && config[1] == 0 && output[1] == 0xff);
        assert(!(output[0] & IOE_MODE) && (output[0] & 0x80));
        periph_resume();
        assert(config[0] == 0x64 && config[1] == 0xff && (output[0] & IOE_MODE));
        assert(acc_on && acc_ups == 1 && touch_wakes == 1 && tp_mode == CST836U_MODE_NORMAL);
    }
    reset(); acc_on = false; periph_suspend(false); periph_resume(); assert(!acc_on && !acc_ups);
    reset(); s_ioe = NULL; assert(read_pico_ioe_sleep(true) == ESP_ERR_INVALID_STATE);
    assert(read_pico_ioe_wake() == ESP_ERR_INVALID_STATE && !step);
    for (int f = 1; f <= 4; ++f) { reset(); fault = f; assert(read_pico_ioe_sleep(true) == ESP_FAIL); fault = 0; assert(read_pico_ioe_wake() == ESP_OK); assert(config[0] == 0x64 && config[1] == 0xff); }
    // 下电前先卸轨，失败和 EN 不落均有界返回并恢复触摸/网络；不重启循环。
    // Drop rails before parking; failed commands or stuck EN return boundedly and restore touch/network without reboot loops.
    for (int mode = APP_SLEEP_DEEP; mode <= APP_SLEEP_OFF; ++mode) for (int ack = 0; ack < 2; ++ack) {
        reset(); rails_on = true; error_result = ack ? ESP_OK : ESP_FAIL;
        assert(app_enter_host_sleep(mode) == (ack ? ESP_ERR_TIMEOUT : ESP_FAIL));
        assert(prepared == 1 && syncs == 1 && radio_stops == 1 && resumes == 1 && pauses == 1);
        assert(off_calls == (mode == APP_SLEEP_OFF) && deep_calls == (mode == APP_SLEEP_DEEP ? ack ? 1 : 3 : 0));
        assert(waits == (ack ? 5000 : mode == APP_SLEEP_DEEP ? 400 : 0));
        assert(acc_on && tp_mode == CST836U_MODE_NORMAL && config[0] == 0x64 && config[1] == 0xff);
    }
    reset(); transfer_ok = false; assert(app_enter_host_sleep(APP_SLEEP_OFF) == ESP_ERR_INVALID_STATE && !prepared && !step);
    reset(); ota = true; assert(app_enter_host_sleep(APP_SLEEP_OFF) == ESP_ERR_INVALID_STATE && !pauses && !prepared);
    reset(); avatar_ok=false;assert(app_enter_host_sleep(APP_SLEEP_OFF)==ESP_ERR_INVALID_STATE&&!pauses&&!prepared);avatar_ok=true;
    reset(); assert(app_enter_host_sleep(APP_SLEEP_LIGHT) == ESP_ERR_INVALID_ARG && !pauses && !prepared);
    puts("PASS: exact BSP 22 sleep pull-downs, CW_INT key wake retained, host-off masks, MODE and Port1 parking, touch/accelerometer restore, absent IOE and I2C faults; rails-before-park; bounded PMU fail/stuck EN and upload/OTA guards");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
    c, exe = Path(folder) / 'test.c', Path(folder) / 'test'
    c.write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
