/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * LEDC PWM 音频：环形缓冲、定时器 ISR 把 PCM 写成占空比。
 *
 * LEDC PWM audio: ring buffer plus a timer ISR that writes PCM as duty.
 */

#ifndef _PWM_AUDIO_H_
#define _PWM_AUDIO_H_

#define PWM_AUDIO_VER_MAJOR 1
#define PWM_AUDIO_VER_MINOR 2
#define PWM_AUDIO_VER_PATCH 0

#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "driver/gptimer.h"
#else
#include "driver/timer.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    timer_group_t tg_num;          ///< 定时器组 / Timer group
    timer_idx_t timer_num;         ///< 定时器编号 / Timer index
#endif
    int gpio_num_left;             ///< 左声道 GPIO / Left GPIO
    int gpio_num_right;            ///< 右声道 GPIO / Right GPIO
    ledc_channel_t ledc_channel_left;   ///< 左 LEDC 通道 / Left LEDC channel
    ledc_channel_t ledc_channel_right;  ///< 右 LEDC 通道 / Right LEDC channel
    ledc_timer_t ledc_timer_sel;   ///< LEDC 定时器 / LEDC timer
    ledc_timer_bit_t duty_resolution;   ///< 占空比分辨率 / Duty resolution
    uint32_t ringbuf_len;          ///< 环形缓冲字节数 / Ring-buffer bytes
} pwm_audio_config_t;

typedef enum {
    PWM_AUDIO_STATUS_UN_INIT = 0,  ///< 未初始化 / Uninitialized
    PWM_AUDIO_STATUS_IDLE = 1,     ///< 空闲 / Idle
    PWM_AUDIO_STATUS_BUSY = 2,     ///< 播放中 / Playing
} pwm_audio_status_t;

typedef enum {
    PWM_AUDIO_CH_MONO = 0,         ///< 单声道 / Mono
    PWM_AUDIO_CH_STEREO = 1,       ///< 立体声 / Stereo
    PWM_AUDIO_CH_MAX,
} pwm_audio_channel_t;

esp_err_t pwm_audio_init(const pwm_audio_config_t *cfg);
esp_err_t pwm_audio_deinit(void);
esp_err_t pwm_audio_start(void);
esp_err_t pwm_audio_stop(void);
esp_err_t pwm_audio_write(
    uint8_t *inbuf, size_t len, size_t *bytes_written, TickType_t ticks_to_wait
);
esp_err_t pwm_audio_set_param(int rate, ledc_timer_bit_t bits, int ch);
esp_err_t pwm_audio_set_sample_rate(int rate);
esp_err_t pwm_audio_set_volume(int8_t volume);
esp_err_t pwm_audio_get_volume(int8_t *volume);
esp_err_t pwm_audio_get_param(int *rate, int *bits, int *ch);
esp_err_t pwm_audio_get_status(pwm_audio_status_t *status);

#ifdef __cplusplus
}
#endif

#endif
