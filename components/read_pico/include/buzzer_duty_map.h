/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 零中心 PCM → 低端开关占空比。不取绝对值、不半波整流。
 * 参数是待测电气工作点，不是安全额定，也不等于硬件限流。
 *
 * Zero-centered PCM to low-side switch duty. No abs, no half-wave
 * rectify. The parameters are experimental electrical setpoints, not
 * safety ratings and not a hardware current limit.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUZZER_DUTY_FS 1023
#define BUZZER_DUTY_GAIN_MAX 32767

typedef struct {
    uint16_t bias;
    uint16_t gain;
    uint16_t duty_min;
    uint16_t duty_max;
} buzzer_duty_map_t;

#define BUZZER_DUTY_MAP_EXPERIMENTAL \
    { .bias = 512, .gain = 3600, .duty_min = 0, .duty_max = 1023 }

int buzzer_duty_map_valid(const buzzer_duty_map_t *m);
int32_t buzzer_duty_legal_pcm(const buzzer_duty_map_t *m);
uint16_t buzzer_duty_from_sample(const buzzer_duty_map_t *m, int16_t pcm);
uint16_t buzzer_duty_from_sample_ex(const buzzer_duty_map_t *m, int16_t pcm, int *clipped);
int16_t buzzer_duty_to_pwm_audio_pcm(uint16_t duty);
void buzzer_duty_map_pcm(const buzzer_duty_map_t *m, const int16_t *in, int16_t *out, int n);

typedef struct {
    uint16_t min;
    uint16_t max;
    uint32_t n;
    uint32_t clips;
    uint64_t sum;
    uint64_t sum_sq;
} buzzer_duty_accum_t;

void buzzer_duty_accum_reset(buzzer_duty_accum_t *a);
void buzzer_duty_accum_add(buzzer_duty_accum_t *a, uint16_t duty, int clipped);
uint32_t buzzer_u64_isqrt(uint64_t x);
void buzzer_duty_accum_result(
    const buzzer_duty_accum_t *a, uint16_t *min, uint16_t *max, uint32_t *mean_x100,
    uint32_t *ac_rms_x100, uint32_t *n, uint32_t *clips
);

#ifdef __cplusplus
}
#endif
