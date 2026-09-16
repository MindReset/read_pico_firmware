/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 无 ESP 依赖的曲谱合成：480 tick/四分、十二平均律、固定增益混音。
 * 主机与固件共用，实时路径不分配内存。
 *
 * ESP-free score synthesis: 480 ticks per quarter, 12-TET, fixed-gain
 * mix. Shared by host and firmware; the realtime path allocates nothing.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "buzzer_duty_map.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUZZER_SCORE_SR 32000
#define BUZZER_SCORE_BPM 135
#define BUZZER_SCORE_PPQ 480
#define BUZZER_SCORE_TICKS 7680
#define BUZZER_SCORE_VOICES 12
#define BUZZER_SCORE_WT 256
#define BUZZER_SCORE_BARS 4
#define BUZZER_SCORE_BAR_TICKS 1920

typedef struct {
    uint32_t tick;
    uint16_t dur;
    uint16_t id;
    uint8_t midi;
    uint8_t vel;
    uint8_t staff;
} buzzer_score_note_t;

typedef struct {
    int32_t peak;
    uint32_t clips;
    uint32_t samples;
    uint32_t max_voices;
    uint32_t note_ons;
    uint32_t note_offs;
    uint64_t sum_sq;
} buzzer_score_stats_t;

typedef enum {
    BUZZER_SCORE_TIMBRE_SOFT = 0,
    BUZZER_SCORE_TIMBRE_BRIGHT = 1,
    BUZZER_SCORE_TIMBRE_ENHANCE = 2,
} buzzer_score_timbre_t;

typedef enum {
    BUZZER_SCORE_ENV_PIANO = 0,
    BUZZER_SCORE_ENV_HOLD = 1,
} buzzer_score_env_t;

#define BUZZER_SCORE_MIX_GAIN_SHIFT 12

#define BUZZER_SCORE_STAFF_RH 1u
#define BUZZER_SCORE_STAFF_LH 2u
#define BUZZER_SCORE_STAFF_BOTH 3u

typedef struct {
    uint16_t w1;
    uint16_t w2;
    uint16_t w3;
    uint16_t w5;
} buzzer_score_harmonics_t;

void buzzer_score_init(void);
void buzzer_score_reset(buzzer_score_stats_t *st);
bool buzzer_score_render(int16_t *dst, int n, buzzer_score_stats_t *st);
bool buzzer_score_idle(void);
void buzzer_score_set_timbre(buzzer_score_timbre_t timbre);
buzzer_score_timbre_t buzzer_score_timbre(void);
void buzzer_score_set_staff_mask(uint8_t mask);
uint8_t buzzer_score_staff_mask(void);
void buzzer_score_set_semitone_shift(int8_t semitones);
int8_t buzzer_score_semitone_shift(void);
void buzzer_score_set_env(buzzer_score_env_t env);
buzzer_score_env_t buzzer_score_env(void);
int32_t buzzer_score_prepare(const buzzer_duty_map_t *map, const volatile bool *abort);
int32_t buzzer_score_raw_peak(void);
int32_t buzzer_score_mix_gain_q12(void);
void buzzer_score_set_harmonics(const buzzer_score_harmonics_t *h);
void buzzer_score_get_harmonics(buzzer_score_harmonics_t *h);
uint32_t buzzer_score_midi_inc(uint8_t midi);
void buzzer_score_render_hold(
    int16_t *dst, int n, uint32_t *phase, uint32_t inc, const buzzer_score_harmonics_t *h,
    int32_t amp
);
uint32_t buzzer_score_pcm_rms(const buzzer_score_stats_t *st);

uint32_t buzzer_score_tick_to_sample(uint32_t tick);
uint32_t buzzer_score_body_samples(void);
int buzzer_score_note_count(void);
const buzzer_score_note_t *buzzer_score_notes(void);

#ifdef __cplusplus
}
#endif
