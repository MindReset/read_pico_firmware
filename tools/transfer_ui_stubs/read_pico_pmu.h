/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：read_pico_pmu 的宿主测试替身，只覆盖 push_sink 用到的电量面。
 * English: host shim for read_pico_pmu, covering only the battery face push_sink uses.
 * 冻结：仅供测试。/ Frozen: tests only.
 */
#pragma once
#include <stdbool.h>

typedef struct {
    bool present;
    bool charging;
    int percent;
} pmu_snapshot_t;

static inline int pmu_battery_percent(const pmu_snapshot_t *p) {
    return p && p->present ? p->percent : -1;
}

static inline bool pmu_battery_charging(const pmu_snapshot_t *p) {
    return p && p->present && p->charging;
}

// 宿主的 PMU 默认「在、不在充电、满电」：武装页的充电常开分支因此可走，电量
// 门槛永不触发；test_pmu_snapshot 可改，回归据此演练插电常开切换。
// The host PMU defaults to present, off the charger and full: the arm page's
// charger-held branch is reachable while the battery gate never fires.
// test_pmu_snapshot is mutable so the regression can rehearse the charger.
static pmu_snapshot_t test_pmu_snapshot = {true, false, 100};
static inline const pmu_snapshot_t *read_pico_pmu_get(void) {
    return &test_pmu_snapshot;
}
