/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * GPIO2 → AO3400A 栅极，高导通低关断。停播必须禁用 PWM 并拉低。
 * 曲谱合成是零中心 PCM；硬件占空比另经 bias/gain/min/max 映射。
 * 高频 PWM 默认不初始化、不上电自测。
 *
 * GPIO2 drives the AO3400A gate: high on, low off. Stop must disable PWM
 * and drive the pin low. Score synthesis is zero-centered PCM; hardware
 * duty is mapped separately via bias/gain/min/max. HF PWM is off by
 * default and is not a power-on self-test.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "buzzer_duty_map.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    READ_PICO_BUZZER_PATH_CLASSIC = 0,
    READ_PICO_BUZZER_PATH_HF = 1,
    READ_PICO_BUZZER_PATH_DIRECT_1BIT = 2,
} read_pico_buzzer_path_t;

typedef enum {
    READ_PICO_BUZZER_PROBE_A_15K = 0,
    READ_PICO_BUZZER_PROBE_B_15K = 1,
    READ_PICO_BUZZER_PROBE_1BIT = 2,
    READ_PICO_BUZZER_PROBE_4K = 3,
} read_pico_buzzer_probe_t;

typedef struct {
    uint32_t carrier_hz;
    uint8_t resolution_bits;
    uint8_t path;
    uint16_t duty_min;
    uint16_t duty_max;
    uint32_t duty_mean_x100;
    uint32_t duty_ac_rms_x100;
    uint32_t duty_clips;
    uint32_t underruns;
    uint32_t samples;
    int32_t pcm_peak;
    uint32_t pcm_rms;
    uint8_t current_ma_valid;
    int16_t current_ma;
    uint32_t tone_hz;
    uint32_t tone_hz_actual;
    int8_t shift_st;
    uint16_t duty_fs;
    uint16_t duty_hw_max;
    uint8_t residual_low_ticks;
    uint8_t voices;
} read_pico_buzzer_out_stats_t;

esp_err_t read_pico_buzzer_tone(uint32_t frequency_hz, uint32_t duration_ms);

esp_err_t read_pico_buzzer_score_play(void);
esp_err_t read_pico_buzzer_score_stop(void);
bool read_pico_buzzer_score_busy(void);
esp_err_t read_pico_buzzer_probe(read_pico_buzzer_probe_t probe);

void read_pico_buzzer_out_stats_get(read_pico_buzzer_out_stats_t *st);

uint32_t read_pico_buzzer_pwm_carrier_hz(void);
uint32_t read_pico_buzzer_pwm_carrier_hz_nominal(void);

void read_pico_buzzer_duty_map_get(buzzer_duty_map_t *map);
esp_err_t read_pico_buzzer_duty_map_set(const buzzer_duty_map_t *map);

esp_err_t read_pico_buzzer_effect(void);

#ifdef __cplusplus
}
#endif
