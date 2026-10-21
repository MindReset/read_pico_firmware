/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
/* r95 起 app_loop.c 引用 read_pico_crashlog.h 的开机分段计时（rp_boot_mark）。
 * Since r95 app_loop.c includes read_pico_crashlog.h for boot-phase marks.
 * 本测试桩只覆盖当前引用面的这一个符号：app_loop_host_test.c 不断言开机分段
 * 计时行为，缺符号时应以链接错误暴露，故不做全接口 no-op 化。
 * This stub covers only the one symbol currently referenced: the host test makes
 * no assertion on boot-phase marks, so a missing symbol must surface as a link
 * error rather than being silently absorbed by a full no-op interface. */
static inline void rp_boot_mark(const char *name) { (void)name; }
