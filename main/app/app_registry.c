/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 全部 demo 页的清单。菜单顺序。加页只改这张表。
 *
 * Demo page table. Menu order lives here; adding a page only changes this list.
 */

#include "app_registry.h"

extern const app_desc_t app_home;
extern const app_desc_t app_refresh;
extern const app_desc_t app_reading;
extern const app_desc_t app_touch;
extern const app_desc_t app_axis;
extern const app_desc_t app_axis_lab;
extern const app_desc_t app_power;
extern const app_desc_t app_pmu;
extern const app_desc_t app_key;
extern const app_desc_t app_sleep;
extern const app_desc_t app_sd;
extern const app_desc_t app_font_pick;
extern const app_desc_t app_ioe;
extern const app_desc_t app_selftest;

// 一份菜单，相近的页挨着排，由 ui_menu 按页翻。/ One menu, similar pages together; ui_menu pages through it.
static const app_desc_t* const s_apps[] = {
    &app_home,
    &app_refresh,
    &app_reading,
    &app_touch,
    &app_axis,
    &app_axis_lab,
    &app_power,
    &app_pmu,
    &app_key,
    &app_sleep,
    &app_sd,
    &app_font_pick,
    &app_ioe,
    &app_selftest,
};

#define APP_COUNT ((int)(sizeof(s_apps) / sizeof(s_apps[0])))

int app_count(void) {
    return APP_COUNT;
}

const app_desc_t* app_at(int index) {
    if (index < 0 || index >= APP_COUNT) return NULL;
    return s_apps[index];
}

int app_index_of(const app_desc_t* app) {
    for (int i = 0; i < APP_COUNT; i++) {
        if (s_apps[i] == app) return i;
    }
    return -1;
}

const app_desc_t* app_home_page(void) {
    return &app_home;
}

const app_desc_t* app_selftest_page(void) {
    return &app_selftest;
}
