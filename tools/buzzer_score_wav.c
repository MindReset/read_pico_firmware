/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 主机离线渲染曲谱为 32 kHz / 16-bit 单声道 WAV，并核对小节、延音、重触发与混音。
 *
 * cc -O2 -std=c11 -I../components/read_pico/include \
 *    -o buzzer_score_wav buzzer_score_wav.c \
 *    ../components/read_pico/buzzer_score.c \
 *    ../components/read_pico/buzzer_duty_map.c -lm
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buzzer_duty_map.h"
#include "buzzer_score.h"

#define CHUNK 512
#define FAIL_MAX 16
#define SAMPLE_ROOM (BUZZER_SCORE_SR * 9)
#define REL_SLACK (BUZZER_SCORE_SR / 4)

static int g_fail;
static char g_why[FAIL_MAX][160];

static void check(int ok, const char *msg) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", msg);
    if (!ok && g_fail < FAIL_MAX) {
        snprintf(g_why[g_fail], sizeof(g_why[0]), "%s", msg);
        g_fail++;
    }
}

static void write_u16(FILE *f, uint16_t v) {
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    fwrite(b, 1, 2, f);
}

static void write_u32(FILE *f, uint32_t v) {
    uint8_t b[4] = {
        (uint8_t)v,
        (uint8_t)(v >> 8),
        (uint8_t)(v >> 16),
        (uint8_t)(v >> 24),
    };
    fwrite(b, 1, 4, f);
}

static int write_wav(const char *path, const int16_t *pcm, uint32_t n) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    uint32_t data_bytes = n * 2u;
    fwrite("RIFF", 1, 4, f);
    write_u32(f, 36 + data_bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    write_u32(f, 16);
    write_u16(f, 1);
    write_u16(f, 1);
    write_u32(f, BUZZER_SCORE_SR);
    write_u32(f, BUZZER_SCORE_SR * 2u);
    write_u16(f, 2);
    write_u16(f, 16);
    fwrite("data", 1, 4, f);
    write_u32(f, data_bytes);
    fwrite(pcm, 2, n, f);
    fclose(f);
    return 0;
}

static void check_bars(void) {
    const buzzer_score_note_t *notes = buzzer_score_notes();
    const int n = buzzer_score_note_count();
    for (int staff = 0; staff < 2; staff++) {
        for (int bar = 0; bar < BUZZER_SCORE_BARS; bar++) {
            uint32_t t0 = (uint32_t)bar * BUZZER_SCORE_BAR_TICKS;
            uint32_t t1 = t0 + BUZZER_SCORE_BAR_TICKS;
            uint32_t start_min = UINT32_MAX;
            uint32_t end_max = 0;
            for (int i = 0; i < n; i++) {
                if (notes[i].staff != (uint8_t)staff) {
                    continue;
                }
                if (notes[i].tick < t0 || notes[i].tick >= t1) {
                    continue;
                }
                if (notes[i].tick < start_min) {
                    start_min = notes[i].tick;
                }
                uint32_t end = notes[i].tick + notes[i].dur;
                if (end > end_max) {
                    end_max = end;
                }
            }
            char msg[96];
            snprintf(msg, sizeof(msg), "staff %d bar %d covers %u..%u", staff, bar + 1, start_min, end_max);
            check(start_min == t0 && end_max == t1, msg);
        }
    }
}

static void check_score_shape(void) {
    const buzzer_score_note_t *notes = buzzer_score_notes();
    const int n = buzzer_score_note_count();
    uint32_t last = 0;
    for (int i = 0; i < n; i++) {
        uint32_t end = notes[i].tick + notes[i].dur;
        if (end > last) {
            last = end;
        }
    }
    check(last == BUZZER_SCORE_TICKS, "score span is 7680 ticks");

    uint32_t body = buzzer_score_body_samples();
    uint32_t expect = (uint32_t)((7680ull * 800ull) / 27ull);
    check(body == expect, "tick-to-sample uses 800/27");
    check(body == 227555, "body samples 227555 (7680*800/27)");

    uint8_t last_rh_bar3 = 0;
    uint32_t last_rh_bar3_tick = 0;
    for (int i = 0; i < n; i++) {
        if (notes[i].staff != 0) {
            continue;
        }
        if (notes[i].tick < 3840 || notes[i].tick >= 5760) {
            continue;
        }
        if (notes[i].tick >= last_rh_bar3_tick) {
            last_rh_bar3_tick = notes[i].tick;
            last_rh_bar3 = notes[i].midi;
        }
    }
    check(last_rh_bar3 == 78, "bar 3 last RH pitch is F#5");
}

static int count_on_at(uint8_t staff, uint32_t tick, uint8_t midi) {
    const buzzer_score_note_t *notes = buzzer_score_notes();
    const int n = buzzer_score_note_count();
    int c = 0;
    for (int i = 0; i < n; i++) {
        if (notes[i].staff == staff && notes[i].tick == tick && notes[i].midi == midi) {
            c++;
        }
    }
    return c;
}

static void check_ties_and_retriggers(void) {
    check(count_on_at(1, 0, 52) == 1 && count_on_at(1, 240, 52) == 0 &&
              count_on_at(1, 960, 52) == 0 && count_on_at(1, 1200, 52) == 1,
          "LH bar1 E3 tied then retrigger at 1200");
    check(count_on_at(1, 1920, 53) == 1 && count_on_at(1, 2160, 53) == 0 &&
              count_on_at(1, 3120, 53) == 1,
          "LH bar2 F3 tied then retrigger at 3120");
    check(count_on_at(0, 0, 76) == 1 && count_on_at(0, 240, 76) == 1 &&
              count_on_at(0, 360, 76) == 1,
          "RH repeated E5 retriggers");
    check(count_on_at(1, 3840, 62) == 1 && count_on_at(1, 4800, 62) == 1,
          "LH bar3 D4 retriggers at mid-bar");
    check(count_on_at(1, 5760, 62) == 1 && count_on_at(1, 7200, 62) == 1,
          "LH bar4 D4 retriggers on last beat");
}

static void check_last_beat(void) {
    const buzzer_score_note_t *notes = buzzer_score_notes();
    const int n = buzzer_score_note_count();
    int rh = 0, lh = 0;
    uint16_t d4_id[2] = { 0, 0 };
    int d4n = 0;
    uint32_t samp = buzzer_score_tick_to_sample(7200);
    int same_sample = 1;
    for (int i = 0; i < n; i++) {
        if (notes[i].tick != 7200) {
            continue;
        }
        if (notes[i].staff == 0) {
            rh++;
        } else {
            lh++;
        }
        if (notes[i].midi == 62 && d4n < 2) {
            d4_id[d4n++] = notes[i].id;
        }
        if (buzzer_score_tick_to_sample(notes[i].tick) != samp) {
            same_sample = 0;
        }
    }
    check(same_sample, "last-beat events share one sample");
    check(rh == 3 && lh == 3, "last beat is 3+3 notes");
    check(d4n == 2 && d4_id[0] != d4_id[1], "two last-beat D4s have distinct ids");
}

static void check_duty_map(void) {
    const buzzer_duty_map_t map = BUZZER_DUTY_MAP_EXPERIMENTAL;
    check(buzzer_duty_map_valid(&map), "experimental duty map is valid");
    check(buzzer_duty_from_sample(&map, 0) == 512, "sample 0 maps to bias");
    check(buzzer_duty_from_sample(&map, 4000) == 951, "score-level + is not rectified");
    check(buzzer_duty_from_sample(&map, -4000) == 73, "score-level - is not abs()");
    uint16_t pos = buzzer_duty_from_sample(&map, 4000);
    uint16_t neg = buzzer_duty_from_sample(&map, -4000);
    check(pos - 512 == 512 - neg, "±sample stay symmetric around bias");
    check(buzzer_duty_from_sample(&map, 32767) == map.duty_max, "full-scale + clamps");
    check(buzzer_duty_from_sample(&map, -32768) == map.duty_min, "full-scale - clamps");

    buzzer_duty_map_t bad = map;
    bad.duty_min = 800;
    bad.duty_max = 100;
    check(!buzzer_duty_map_valid(&bad), "min>max rejected");

    uint16_t d = 512;
    int16_t pcm = buzzer_duty_to_pwm_audio_pcm(d);
    check((uint16_t)((pcm + 32767) >> 6) == d, "pwm_audio inverse is exact");
}

static void report_config(
    const char *name, buzzer_score_timbre_t timbre, buzzer_score_env_t env, int8_t shift,
    uint8_t staff, int16_t *save, uint32_t *filled, uint32_t room, buzzer_score_stats_t *out
) {
    buzzer_duty_map_t map = BUZZER_DUTY_MAP_EXPERIMENTAL;
    buzzer_score_set_timbre(timbre);
    buzzer_score_set_env(env);
    buzzer_score_set_semitone_shift(shift);
    buzzer_score_set_staff_mask(staff);
    int32_t target = buzzer_duty_legal_pcm(&map);
    int32_t gain = buzzer_score_prepare(&map, NULL);

    buzzer_score_stats_t st;
    buzzer_score_reset(&st);
    buzzer_duty_accum_t acc;
    buzzer_duty_accum_reset(&acc);
    int16_t chunk[CHUNK];
    uint32_t nfill = 0;
    while (buzzer_score_render(chunk, CHUNK, &st)) {
        for (int i = 0; i < CHUNK; i++) {
            int clip = 0;
            uint16_t d = buzzer_duty_from_sample_ex(&map, chunk[i], &clip);
            buzzer_duty_accum_add(&acc, d, clip);
        }
        if (save != NULL) {
            if (nfill + CHUNK > room) {
                break;
            }
            memcpy(save + nfill, chunk, CHUNK * sizeof(int16_t));
        }
        nfill += CHUNK;
    }
    uint16_t dmin = 0, dmax = 0;
    uint32_t mean = 0, ac = 0, n = 0, clips = 0;
    buzzer_duty_accum_result(&acc, &dmin, &dmax, &mean, &ac, &n, &clips);
    printf("%s target=%d raw_peak=%d gain_q12=%d samples %u peak %d rms %u clips %u "
           "max_voices %u duty %u..%u ac_rms %u.%02u duty_clips %u\n",
           name, target, buzzer_score_raw_peak(), gain, st.samples, st.peak,
           buzzer_score_pcm_rms(&st), st.clips, st.max_voices, dmin, dmax, ac / 100, ac % 100,
           clips);
    if (filled != NULL) {
        *filled = nfill;
    }
    if (out != NULL) {
        *out = st;
    }
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/tmp/read_pico_buzzer_score.wav";

    buzzer_score_init();
    check_bars();
    check_score_shape();
    check_ties_and_retriggers();
    check_last_beat();
    check_duty_map();

    buzzer_duty_map_t map = BUZZER_DUTY_MAP_EXPERIMENTAL;
    int32_t target = buzzer_duty_legal_pcm(&map);
    check(target >= 4500 && target <= 4700, "legal pcm uses existing duty swing");

    int16_t *pcm = (int16_t *)calloc((size_t)SAMPLE_ROOM, sizeof(int16_t));
    if (pcm == NULL) {
        fprintf(stderr, "oom\n");
        return 2;
    }

    buzzer_score_stats_t piano;
    uint32_t filled = 0;
    report_config(
        "bright_piano_+24", BUZZER_SCORE_TIMBRE_BRIGHT, BUZZER_SCORE_ENV_PIANO, 24,
        BUZZER_SCORE_STAFF_BOTH, pcm, &filled, SAMPLE_ROOM, &piano
    );
    check(piano.samples + REL_SLACK >= buzzer_score_body_samples(), "rendered through score body");
    check((int32_t)piano.samples - (int32_t)buzzer_score_body_samples() <= (int32_t)BUZZER_SCORE_SR,
          "tail within one second of body");
    check(piano.peak <= 32767 && piano.clips == 0, "mix peak within int16, no clip");
    check(piano.max_voices <= BUZZER_SCORE_VOICES, "voices within 12");
    check(piano.max_voices >= 6, "last beat uses at least 6 voices");
    check(buzzer_score_idle(), "idle after all releases");
    check(piano.peak <= target + 8, "prepared peak stays within duty swing");

    if (write_wav(path, pcm, filled) != 0) {
        fprintf(stderr, "write %s failed\n", path);
        free(pcm);
        return 2;
    }
    free(pcm);
    printf("wav %s\n", path);

    buzzer_score_stats_t hold;
    report_config(
        "bright_hold_+24", BUZZER_SCORE_TIMBRE_BRIGHT, BUZZER_SCORE_ENV_HOLD, 24,
        BUZZER_SCORE_STAFF_BOTH, NULL, NULL, 0, &hold
    );
    check(hold.clips == 0 && hold.peak <= target + 8, "hold prepared peak within duty swing");

    report_config(
        "enhance_piano_+24", BUZZER_SCORE_TIMBRE_ENHANCE, BUZZER_SCORE_ENV_PIANO, 24,
        BUZZER_SCORE_STAFF_BOTH, NULL, NULL, 0, NULL
    );

    buzzer_score_stats_t rh;
    report_config(
        "bright_piano_+24_rh", BUZZER_SCORE_TIMBRE_BRIGHT, BUZZER_SCORE_ENV_PIANO, 24,
        BUZZER_SCORE_STAFF_RH, NULL, NULL, 0, &rh
    );
    check(rh.max_voices < piano.max_voices && rh.max_voices <= 5, "RH-only uses fewer voices");

    if (g_fail) {
        fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }
    return 0;
}
