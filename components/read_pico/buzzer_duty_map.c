/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * duty = clamp(bias + gain * sample, min, max)，sample = pcm / 32768。
 * pwm_audio 16bit/10bit：duty = (pcm + 32767) >> 6，这里做逆变换。
 *
 * duty = clamp(bias + gain * sample, min, max), sample = pcm / 32768.
 * pwm_audio 16-bit/10-bit: duty = (pcm + 32767) >> 6; this inverts that map.
 */

#include "buzzer_duty_map.h"

#include <stddef.h>

int buzzer_duty_map_valid(const buzzer_duty_map_t *m) {
    if (m == NULL) {
        return 0;
    }
    if (m->duty_min > m->duty_max) {
        return 0;
    }
    if (m->duty_max > BUZZER_DUTY_FS) {
        return 0;
    }
    if (m->bias > BUZZER_DUTY_FS || m->gain > BUZZER_DUTY_GAIN_MAX) {
        return 0;
    }
    return 1;
}

int32_t buzzer_duty_legal_pcm(const buzzer_duty_map_t *m) {
    if (!buzzer_duty_map_valid(m) || m->gain == 0) {
        return 0;
    }
    int32_t up = ((int32_t)m->duty_max - (int32_t)m->bias) * 32768 / (int32_t)m->gain;
    int32_t dn = ((int32_t)m->bias - (int32_t)m->duty_min) * 32768 / (int32_t)m->gain;
    if (up < 0) {
        up = 0;
    }
    if (dn < 0) {
        dn = 0;
    }
    int32_t t = up < dn ? up : dn;
    return (t * 255) / 256;
}

uint16_t buzzer_duty_from_sample_ex(const buzzer_duty_map_t *m, int16_t pcm, int *clipped) {
    int32_t duty = (int32_t)m->bias + ((int32_t)m->gain * (int32_t)pcm) / 32768;
    int clip = 0;
    if (duty < (int32_t)m->duty_min) {
        duty = (int32_t)m->duty_min;
        clip = 1;
    }
    if (duty > (int32_t)m->duty_max) {
        duty = (int32_t)m->duty_max;
        clip = 1;
    }
    if (clipped != NULL) {
        *clipped = clip;
    }
    return (uint16_t)duty;
}

uint16_t buzzer_duty_from_sample(const buzzer_duty_map_t *m, int16_t pcm) {
    return buzzer_duty_from_sample_ex(m, pcm, NULL);
}

int16_t buzzer_duty_to_pwm_audio_pcm(uint16_t duty) {
    if (duty > BUZZER_DUTY_FS) {
        duty = BUZZER_DUTY_FS;
    }
    int32_t s = ((int32_t)duty << 6) - 32767;
    if (s > 32767) {
        s = 32767;
    }
    if (s < -32768) {
        s = -32768;
    }
    return (int16_t)s;
}

void buzzer_duty_map_pcm(const buzzer_duty_map_t *m, const int16_t *in, int16_t *out, int n) {
    for (int i = 0; i < n; i++) {
        out[i] = buzzer_duty_to_pwm_audio_pcm(buzzer_duty_from_sample(m, in[i]));
    }
}

void buzzer_duty_accum_reset(buzzer_duty_accum_t *a) {
    if (a == NULL) {
        return;
    }
    a->min = 0xffff;
    a->max = 0;
    a->n = 0;
    a->clips = 0;
    a->sum = 0;
    a->sum_sq = 0;
}

void buzzer_duty_accum_add(buzzer_duty_accum_t *a, uint16_t duty, int clipped) {
    if (a == NULL) {
        return;
    }
    if (duty < a->min) {
        a->min = duty;
    }
    if (duty > a->max) {
        a->max = duty;
    }
    a->n++;
    a->sum += duty;
    a->sum_sq += (uint64_t)duty * (uint64_t)duty;
    if (clipped) {
        a->clips++;
    }
}

uint32_t buzzer_u64_isqrt(uint64_t x) {
    if (x == 0) {
        return 0;
    }
    uint64_t op = x;
    uint64_t res = 0;
    uint64_t one = 1ull << 62;
    while (one > op) {
        one >>= 2;
    }
    while (one != 0) {
        if (op >= res + one) {
            op -= res + one;
            res = (res >> 1) + one;
        } else {
            res >>= 1;
        }
        one >>= 2;
    }
    return (uint32_t)res;
}

void buzzer_duty_accum_result(
    const buzzer_duty_accum_t *a, uint16_t *min, uint16_t *max, uint32_t *mean_x100,
    uint32_t *ac_rms_x100, uint32_t *n, uint32_t *clips
) {
    uint32_t nn = a != NULL ? a->n : 0;
    if (n != NULL) {
        *n = nn;
    }
    if (clips != NULL) {
        *clips = a != NULL ? a->clips : 0;
    }
    if (nn == 0) {
        if (min != NULL) {
            *min = 0;
        }
        if (max != NULL) {
            *max = 0;
        }
        if (mean_x100 != NULL) {
            *mean_x100 = 0;
        }
        if (ac_rms_x100 != NULL) {
            *ac_rms_x100 = 0;
        }
        return;
    }
    if (min != NULL) {
        *min = a->min;
    }
    if (max != NULL) {
        *max = a->max;
    }
    if (mean_x100 != NULL) {
        *mean_x100 = (uint32_t)((a->sum * 100ull) / nn);
    }
    if (ac_rms_x100 != NULL) {
        uint64_t num = a->sum_sq * (uint64_t)nn;
        uint64_t den = a->sum * a->sum;
        if (num <= den) {
            *ac_rms_x100 = 0;
        } else {
            *ac_rms_x100 = buzzer_u64_isqrt((num - den) * 10000ull / ((uint64_t)nn * (uint64_t)nn));
        }
    }
}
