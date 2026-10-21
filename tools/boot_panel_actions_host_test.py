#!/usr/bin/env python3
"""中文：执行真实 RTC 标记和设置修复动作，验证开机截止、取消与单次推屏。
English: Execute real RTC marks and Settings repair actions for boot cutoff, cancellation and single presentation.
SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
CRASH = ROOT / 'components/read_pico'
SETTINGS = ROOT / 'main/apps/app_device_settings.c'

BOOT = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static int64_t clock_us;
int64_t esp_timer_get_time(void) { return clock_us; }
#include "read_pico_crashlog.c"
int main(void) {
    rp_crash_log_clear();
    assert(!rp_crash_log_count());
    const char *phases[] = {"hw","vcom","splash","font","lock","resume","sdfont","scan","excerpt","enter","frame"};
    for (unsigned i = 0; i < sizeof(phases)/sizeof(phases[0]); ++i) {
        clock_us = (int64_t)(i + 1) * 1000000;
        rp_boot_mark(phases[i]);
    }
    assert(rp_crash_log_count() == 11 && strstr(rp_crash_log_line(0), "boot frame 11000ms"));
    uint32_t serial = rp_crash_log_serial();
    for (unsigned i = 0; i < 10000; ++i) { rp_boot_mark("scan"); rp_boot_mark("excerpt"); }
    assert(rp_crash_log_serial() == serial && rp_crash_log_count() == 11);
    rp_crash_log("still records runtime");
    assert(rp_crash_log_serial() == serial + 1);
    rp_crash_log_clear();
    rp_boot_mark("scan");
    assert(!rp_crash_log_count());
    // 模拟下次启动仅重置普通 BSS，RTC 数据由原接口管理。/ Simulate the next boot's BSS reset; RTC stays owned by its existing API.
    s_boot_marks_done = false;
    clock_us = 23000;
    rp_boot_mark(NULL);
    assert(strstr(rp_crash_log_line(0), "boot ? 23ms"));
    char long_name[512]; memset(long_name,'x',sizeof(long_name)-1); long_name[sizeof(long_name)-1]=0;
    rp_boot_mark(long_name);
    assert(strlen(rp_crash_log_line(0)) < RP_CRASH_LOG_LEN);
    for (unsigned i = 0; i < 100; ++i) rp_crash_log("event %u", i);
    assert(rp_crash_log_count() == RP_CRASH_LOG_LINES);
    puts("PASS: all 11 boot marks, bounded RTC ring, post-frame cutoff and normal runtime logging");
}
'''

tick = function('on_tick', SETTINGS)
tick = tick[tick.index('    if (s_panel_repair_pending)'):tick.index('    if (s_page==SETTINGS_MAIN && s_avatar_pending')]
gesture = function('on_gesture', SETTINGS)
gesture = gesture[gesture.index('    if(y>=SETTINGS_MAINTENANCE_Y'):gesture.rindex('    return APP_REDRAW_NONE;')]
render = function('render', SETTINGS)
assert render.index('if (s_notice[0])', render.index('const char *maintenance_labels')) > render.index('ui_fill_rect_fast((EpdRect){0, 0, UI_LOCK_WIDTH, 160}')

ACTIONS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_FULL,APP_REDRAW_DONE} app_redraw_t;
typedef struct { void *hl; uint8_t *fb; } app_ctx_t;
enum { SETTINGS_MAIN, SETTINGS_CONFIG, SETTINGS_BOOT };
enum EpdDrawError { EPD_DRAW_SUCCESS, EPD_DRAW_ERROR };
#define SETTINGS_MAINTENANCE_Y 500
#define SETTINGS_MAINTENANCE_ROWS 4
#define SETTINGS_ROW_H 68
static int s_page, repairs, paints, upgrades;
static bool s_panel_repair_pending, s_scroll_present_pending, s_boot_pending, s_config_confirm;
static char s_notice[96];
static enum EpdDrawError result;
static void upgrade_open(void) { ++upgrades; }
static void render(app_ctx_t *ctx, uint8_t *fb) {
    assert(ctx->fb == fb && strstr(s_notice,"修复完成")); ++paints;
}
static enum EpdDrawError display_panel_repair(void *hl) {
    assert(hl && paints == repairs + 1 && !s_scroll_present_pending);
    ++repairs; return result;
}
static app_redraw_t tick(app_ctx_t *ctx) {
''' + tick + r'''
    return APP_REDRAW_NONE;
}
static app_redraw_t tap(int y) {
    int row;
''' + gesture + r'''
    return APP_REDRAW_NONE;
}
int main(void) {
    uint8_t fb;
    app_ctx_t ctx={.hl=&fb,.fb=&fb};
    assert(tick(&ctx)==APP_REDRAW_NONE && !repairs && !paints);
    assert(tap(SETTINGS_MAINTENANCE_Y)==APP_REDRAW_PAGE && upgrades==1);
    assert(tap(SETTINGS_MAINTENANCE_Y+SETTINGS_ROW_H)==APP_REDRAW_PAGE && s_page==SETTINGS_CONFIG);
    s_page=SETTINGS_MAIN; s_scroll_present_pending=true;
    assert(tap(SETTINGS_MAINTENANCE_Y+2*SETTINGS_ROW_H)==APP_REDRAW_PAGE);
    assert(s_panel_repair_pending && !repairs && !paints && strstr(s_notice,"修复中"));
    assert(tick(&ctx)==APP_REDRAW_DONE && repairs==1 && paints==1 && !s_panel_repair_pending);
    assert(tick(&ctx)==APP_REDRAW_NONE && repairs==1);
    assert(tap(SETTINGS_MAINTENANCE_Y+2*SETTINGS_ROW_H)==APP_REDRAW_PAGE);
    result=EPD_DRAW_ERROR;
    assert(tick(&ctx)==APP_REDRAW_FULL && repairs==2 && strstr(s_notice,"未完成"));
    assert(!s_panel_repair_pending && tick(&ctx)==APP_REDRAW_NONE);
    assert(tap(SETTINGS_MAINTENANCE_Y+2*SETTINGS_ROW_H)==APP_REDRAW_PAGE);
    s_page=SETTINGS_CONFIG;
    assert(tick(&ctx)==APP_REDRAW_NONE && repairs==2 && !s_panel_repair_pending);
    assert(tap(SETTINGS_MAINTENANCE_Y+3*SETTINGS_ROW_H)==APP_REDRAW_PAGE && s_page==SETTINGS_BOOT);
    assert(tap(SETTINGS_MAINTENANCE_Y+4*SETTINGS_ROW_H)==APP_REDRAW_NONE);
    puts("PASS: actual Settings repair row defers work, cancels after leaving, paints final notice once and reports output failure");
}
'''

with tempfile.TemporaryDirectory(prefix='kiikoread-boot-panel-') as directory:
    work = Path(directory)
    (work/'freertos').mkdir()
    (work/'esp_attr.h').write_text('#define RTC_NOINIT_ATTR\n')
    (work/'esp_timer.h').write_text('#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
    (work/'freertos/FreeRTOS.h').write_text('typedef int portMUX_TYPE;\n#define portMUX_INITIALIZER_UNLOCKED 0\n#define portENTER_CRITICAL(p) ((void)(p))\n#define portEXIT_CRITICAL(p) ((void)(p))\n')
    (work/'freertos/task.h').write_text('')
    for name, unit in [('boot', BOOT), ('actions', ACTIONS)]:
        (work/f'{name}.c').write_text(unit, encoding='utf-8')
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
                        '-I'+str(work),'-I'+str(CRASH),'-I'+str(CRASH/'include'),
                        str(work/f'{name}.c'),'-o',str(work/name)],check=True)
        subprocess.run([str(work/name)],check=True)
