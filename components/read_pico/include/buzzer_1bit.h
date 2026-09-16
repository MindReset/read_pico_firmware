/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 把多路音高排成 1bit 脉冲串：按周期排队，滑移超限就丢。
 *
 * Schedule several pitches into a 1-bit pulse train: queue by period,
 * drop a pulse if slip exceeds the limit.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUZZER_1BIT_VOICES_MAX 6
#define BUZZER_1BIT_SYM_MAX 96

typedef struct {
    uint16_t pulse_us;
    uint16_t max_slip_us;
} buzzer_1bit_cfg_t;

typedef struct {
    uint16_t high_us;
    uint16_t low_us;
} buzzer_1bit_sym_t;

typedef struct {
    uint32_t emitted;
    uint32_t dropped;
    uint32_t serialized;
    uint32_t merged_high;
    uint32_t high_us;
    uint32_t window_us;
} buzzer_1bit_stats_t;

#define BUZZER_1BIT_CFG_DEFAULT \
    { .pulse_us = 48, .max_slip_us = 80 }

int buzzer_1bit_build(
    const uint16_t *hz, const uint16_t *weight, int nvoice, const buzzer_1bit_cfg_t *cfg,
    uint32_t window_us, buzzer_1bit_sym_t *out, int max_out, buzzer_1bit_stats_t *st
);

#ifdef __cplusplus
}
#endif
