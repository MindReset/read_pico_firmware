/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 多路音高排成 1bit 脉冲串。
 *
 * Schedule several pitches into a 1-bit pulse train.
 */

#include "buzzer_1bit.h"

int buzzer_1bit_build(
    const uint16_t *hz, const uint16_t *weight, int nvoice, const buzzer_1bit_cfg_t *cfg,
    uint32_t window_us, buzzer_1bit_sym_t *out, int max_out, buzzer_1bit_stats_t *st
) {
    if (hz == NULL || out == NULL || nvoice <= 0 || max_out <= 0 || window_us == 0) {
        return 0;
    }
    if (nvoice > BUZZER_1BIT_VOICES_MAX) {
        nvoice = BUZZER_1BIT_VOICES_MAX;
    }

    buzzer_1bit_cfg_t c = cfg != NULL ? *cfg : (buzzer_1bit_cfg_t)BUZZER_1BIT_CFG_DEFAULT;
    if (c.pulse_us == 0) {
        c.pulse_us = 1;
    }

    uint32_t period[BUZZER_1BIT_VOICES_MAX];
    uint32_t next[BUZZER_1BIT_VOICES_MAX];
    uint16_t width[BUZZER_1BIT_VOICES_MAX];
    int live = 0;
    for (int i = 0; i < nvoice; i++) {
        if (hz[i] == 0) {
            continue;
        }
        period[live] = (1000000u + (uint32_t)hz[i] / 2u) / (uint32_t)hz[i];
        if (period[live] == 0) {
            period[live] = 1;
        }
        uint32_t w = c.pulse_us;
        if (weight != NULL) {
            w = (w * (uint32_t)weight[i]) / 256u;
        }
        if (w == 0) {
            w = 1;
        }
        if (w >= period[live]) {
            w = period[live] - 1;
        }
        width[live] = (uint16_t)w;
        next[live] = (uint32_t)live * c.pulse_us;
        live++;
    }
    if (live == 0) {
        return 0;
    }

    buzzer_1bit_stats_t local = { 0 };
    local.window_us = window_us;
    uint32_t t = 0;
    int nout = 0;

    while (t < window_us && nout < max_out) {
        int pick = 0;
        uint32_t soon = next[0];
        for (int i = 1; i < live; i++) {
            if (next[i] < soon) {
                soon = next[i];
                pick = i;
            }
        }

        uint32_t start = soon;
        if (start < t) {
            uint32_t slip = t - start;
            if (slip > (uint32_t)c.max_slip_us) {
                local.dropped++;
                next[pick] += period[pick];
                continue;
            }
            start = t;
            local.serialized++;
        }
        if (start >= window_us) {
            break;
        }

        uint32_t hi = width[pick];
        if (start + hi > window_us) {
            hi = window_us - start;
        }
        if (hi == 0) {
            break;
        }

        next[pick] += period[pick];
        t = start + hi;
        local.emitted++;
        local.high_us += hi;

        uint32_t nxt = next[0];
        for (int i = 1; i < live; i++) {
            if (next[i] < nxt) {
                nxt = next[i];
            }
        }
        if (nxt < t) {
            nxt = t;
        }
        uint32_t lo = nxt - t;
        if (t + lo > window_us) {
            lo = window_us - t;
        }
        if (lo == 0 && nout > 0) {
            out[nout - 1].high_us = (uint16_t)(out[nout - 1].high_us + (uint16_t)hi);
            local.merged_high++;
            continue;
        }

        out[nout].high_us = (uint16_t)hi;
        out[nout].low_us = (uint16_t)(lo > 65535u ? 65535u : lo);
        nout++;
        t += lo;
    }

    if (st != NULL) {
        *st = local;
    }
    return nout;
}
