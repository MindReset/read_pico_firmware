/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 正弦+弱二次谐波，短起音/衰减/释放。延音线合成一条，同音无连线则重触发。
 * 声部按 note id 归属，左右手同音高互不关闭。
 * 混音增益按整曲峰值一次性定标，不按声部上限或瞬时声部数缩放。
 * ENHANCE：每声部按基频做连续整数谐波，1.2–5 kHz 平滑加权后 L1 归一。
 * 不是对方波频响截图做反向 EQ。
 *
 * Sine plus a weak 2nd harmonic, short attack/decay/release. A tie is one
 * voice; same pitch without a tie retriggers. Voices belong to note id, so
 * left and right hands on the same pitch do not steal each other.
 * Mix gain is scaled once from the whole-piece peak, not by voice cap or
 * live voice count. ENHANCE: integer harmonics of each voice's fundamental,
 * smooth 1.2–5 kHz weights, L1-normalized. Not an inverse EQ of a square
 * frequency-response screenshot.
 */

#include "buzzer_score.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "buzzer_duty_map.h"

#define WT BUZZER_SCORE_WT
#define NVOICE BUZZER_SCORE_VOICES
#define SR BUZZER_SCORE_SR

#define T16 120u
#define T8 240u
#define T4 480u
#define T4D 720u
#define T2 960u
#define T2D 1440u

#define M_E3 52
#define M_F3 53
#define M_G3 55
#define M_A3 57
#define M_B3 59
#define M_C4 60
#define M_D4 62
#define M_F4 65
#define M_G4 67
#define M_A4 69
#define M_B4 71
#define M_C5 72
#define M_D5 74
#define M_E5 76
#define M_F5 77
#define M_FS5 78
#define M_G5 79

#define ATK_N ((SR * 5) / 1000)
#define DEC_N ((SR * 60) / 1000)
#define REL_N ((SR * 72) / 1000)
#define SUS_ENV 15000
#define HOLD_ATK_N ((SR * 4) / 1000)
#define HOLD_DEC_N ((SR * 40) / 1000)
#define HOLD_REL_N ((SR * 20) / 1000)
#define HOLD_SUS 14000
#define MIX_GAIN_SHIFT BUZZER_SCORE_MIX_GAIN_SHIFT
#define SAMPLE_CAP ((uint32_t)SR * 8u)
#define HARM_MAX 24
#define ENHANCE_WSUM 256
#define ENHANCE_ID_MIX 80
#define ENHANCE_EX_MIX 176
#define ENHANCE_EX_SPAN 6

#define ST_OFF 0
#define ST_ON 1
#define ST_REL 2

#define EV_MAX 128

typedef struct {
    uint32_t sample;
    uint16_t id;
    uint8_t midi;
    uint8_t vel;
    uint8_t on;
    uint8_t staff;
} ev_t;

typedef struct {
    uint16_t id;
    uint8_t midi;
    uint8_t vel;
    uint8_t stage;
    uint8_t n_harm;
    uint16_t hw[HARM_MAX];
    uint32_t phase;
    uint32_t inc;
    uint32_t age;
    uint32_t rel_age;
    int32_t env;
    int32_t rel_from;
} voice_t;

static const buzzer_score_note_t k_notes[] = {
    { 0, T8, 1, M_E5, 184, 0 },
    { 240, T16, 2, M_E5, 168, 0 },
    { 360, T16, 3, M_E5, 168, 0 },
    { 480, T8, 4, M_C5, 160, 0 },
    { 720, T8, 5, M_E5, 176, 0 },
    { 960, T8, 6, M_C5, 158, 0 },
    { 1200, T8, 7, M_G4, 150, 0 },
    { 1440, T8, 8, M_C5, 158, 0 },
    { 1680, T8, 9, M_E5, 176, 0 },

    { 1920, T8, 10, M_F5, 180, 0 },
    { 2160, T16, 11, M_F5, 166, 0 },
    { 2280, T16, 12, M_F5, 166, 0 },
    { 2400, T8, 13, M_C5, 156, 0 },
    { 2640, T8, 14, M_F5, 174, 0 },
    { 2880, T8, 15, M_C5, 154, 0 },
    { 3120, T8, 16, M_A4, 148, 0 },
    { 3360, T8, 17, M_D5, 164, 0 },
    { 3600, T8, 18, M_C5, 152, 0 },

    { 3840, T8, 19, M_B4, 168, 0 },
    { 4080, T16, 20, M_B4, 156, 0 },
    { 4200, T16, 21, M_C5, 158, 0 },
    { 4320, T8, 22, M_D5, 164, 0 },
    { 4560, T8, 23, M_E5, 170, 0 },
    { 4800, T8, 24, M_FS5, 176, 0 },
    { 5040, T8, 25, M_D5, 164, 0 },
    { 5280, T8, 26, M_E5, 170, 0 },
    { 5520, T8, 27, M_FS5, 176, 0 },

    { 5760, T8, 28, M_G5, 186, 0 },
    { 6000, T8, 29, M_G5, 180, 0 },
    { 6240, T8, 30, M_D5, 166, 0 },
    { 6480, T8, 31, M_B4, 158, 0 },
    { 6720, T4, 32, M_G4, 170, 0 },
    { 7200, T4, 33, M_D4, 148, 0 },
    { 7200, T4, 34, M_F4, 156, 0 },
    { 7200, T4, 35, M_B4, 188, 0 },

    { 0, 1200, 36, M_E3, 78, 1 },
    { 0, 1200, 37, M_G3, 78, 1 },
    { 0, 1200, 38, M_C4, 82, 1 },
    { 1200, T4D, 39, M_E3, 78, 1 },
    { 1200, T4D, 40, M_G3, 78, 1 },
    { 1200, T4D, 41, M_C4, 82, 1 },

    { 1920, 1200, 42, M_F3, 78, 1 },
    { 1920, 1200, 43, M_A3, 78, 1 },
    { 1920, 1200, 44, M_C4, 82, 1 },
    { 3120, T4D, 45, M_F3, 78, 1 },
    { 3120, T4D, 46, M_A3, 78, 1 },
    { 3120, T4D, 47, M_C4, 82, 1 },

    { 3840, T2, 48, M_F3, 78, 1 },
    { 3840, T2, 49, M_A3, 78, 1 },
    { 3840, T2, 50, M_D4, 82, 1 },
    { 4800, T2, 51, M_F3, 78, 1 },
    { 4800, T2, 52, M_A3, 78, 1 },
    { 4800, T2, 53, M_D4, 82, 1 },

    { 5760, T2D, 54, M_G3, 78, 1 },
    { 5760, T2D, 55, M_B3, 78, 1 },
    { 5760, T2D, 56, M_D4, 82, 1 },
    { 7200, T4, 57, M_G3, 78, 1 },
    { 7200, T4, 58, M_B3, 78, 1 },
    { 7200, T4, 59, M_D4, 82, 1 },
};

static int16_t s_sin[WT];
static uint32_t s_inc[128];
static ev_t s_ev[EV_MAX];
static int s_nev;
static bool s_ready;
static buzzer_score_timbre_t s_timbre = BUZZER_SCORE_TIMBRE_BRIGHT;
static buzzer_score_env_t s_env = BUZZER_SCORE_ENV_PIANO;
static uint8_t s_staff_mask = BUZZER_SCORE_STAFF_BOTH;
static int8_t s_shift = 24;
static int32_t s_mix_gain_q = 1 << MIX_GAIN_SHIFT;
static int32_t s_raw_peak;
static int s_gain_enable;
static const buzzer_score_harmonics_t k_soft = { 256, 42, 0, 0 };
static buzzer_score_harmonics_t s_bright = { 256, 0, 80, 48 };

static voice_t s_voice[NVOICE];
static uint32_t s_pos;
static int s_evi;
static bool s_done;

uint32_t buzzer_score_tick_to_sample(uint32_t tick) {
    return (uint32_t)(((uint64_t)tick * 800ull) / 27ull);
}

uint32_t buzzer_score_body_samples(void) {
    return buzzer_score_tick_to_sample(BUZZER_SCORE_TICKS);
}

int buzzer_score_note_count(void) {
    return (int)(sizeof(k_notes) / sizeof(k_notes[0]));
}

const buzzer_score_note_t *buzzer_score_notes(void) {
    return k_notes;
}

static int ev_cmp(const void *a, const void *b) {
    const ev_t *ea = (const ev_t *)a;
    const ev_t *eb = (const ev_t *)b;
    if (ea->sample != eb->sample) {
        return ea->sample < eb->sample ? -1 : 1;
    }
    if (ea->on != eb->on) {
        return ea->on ? 1 : -1;
    }
    return (int)ea->id - (int)eb->id;
}

void buzzer_score_init(void) {
    if (s_ready) {
        return;
    }
    for (int i = 0; i < WT; i++) {
        s_sin[i] = (int16_t)(sinf((float)i * 6.28318530718f / (float)WT) * 32767.f);
    }
    for (int m = 0; m < 128; m++) {
        double hz = 440.0 * pow(2.0, ((double)m - 69.0) / 12.0);
        s_inc[m] = (uint32_t)(hz * 4294967296.0 / (double)SR + 0.5);
    }

    const int n = buzzer_score_note_count();
    int e = 0;
    for (int i = 0; i < n && e + 1 < EV_MAX; i++) {
        const buzzer_score_note_t *note = &k_notes[i];
        s_ev[e].sample = buzzer_score_tick_to_sample(note->tick);
        s_ev[e].id = note->id;
        s_ev[e].midi = note->midi;
        s_ev[e].vel = note->vel;
        s_ev[e].on = 1;
        s_ev[e].staff = note->staff;
        e++;
        s_ev[e].sample = buzzer_score_tick_to_sample(note->tick + note->dur);
        s_ev[e].id = note->id;
        s_ev[e].midi = note->midi;
        s_ev[e].vel = note->vel;
        s_ev[e].on = 0;
        s_ev[e].staff = note->staff;
        e++;
    }
    s_nev = e;
    qsort(s_ev, (size_t)s_nev, sizeof(s_ev[0]), ev_cmp);
    s_ready = true;
}

void buzzer_score_reset(buzzer_score_stats_t *st) {
    memset(s_voice, 0, sizeof(s_voice));
    s_pos = 0;
    s_evi = 0;
    s_done = false;
    if (st != NULL) {
        memset(st, 0, sizeof(*st));
    }
}

bool buzzer_score_idle(void) {
    if (!s_done) {
        return false;
    }
    for (int i = 0; i < NVOICE; i++) {
        if (s_voice[i].stage != ST_OFF) {
            return false;
        }
    }
    return true;
}

#define HARM_NYQ_LIM 1879048192ull

static int harm_ok(uint32_t inc, unsigned n) {
    return ((uint64_t)inc * (uint64_t)n) < HARM_NYQ_LIM;
}

static const buzzer_score_harmonics_t *active_harmonics(void) {
    return s_timbre == BUZZER_SCORE_TIMBRE_BRIGHT ? &s_bright : &k_soft;
}

static int32_t wave_at(uint32_t phase, uint32_t inc, const buzzer_score_harmonics_t *h) {
    int32_t acc = 0;
    int32_t wsum = 0;
    if (h->w1 != 0 && harm_ok(inc, 1)) {
        acc += (int32_t)s_sin[phase >> 24] * (int32_t)h->w1;
        wsum += (int32_t)h->w1;
    }
    if (h->w2 != 0 && harm_ok(inc, 2)) {
        acc += (int32_t)s_sin[(phase * 2u) >> 24] * (int32_t)h->w2;
        wsum += (int32_t)h->w2;
    }
    if (h->w3 != 0 && harm_ok(inc, 3)) {
        acc += (int32_t)s_sin[(phase * 3u) >> 24] * (int32_t)h->w3;
        wsum += (int32_t)h->w3;
    }
    if (h->w5 != 0 && harm_ok(inc, 5)) {
        acc += (int32_t)s_sin[(phase * 5u) >> 24] * (int32_t)h->w5;
        wsum += (int32_t)h->w5;
    }
    if (wsum <= 0) {
        return 0;
    }
    return acc / wsum;
}

static int32_t enhance_id_w(unsigned k) {
    if (k == 1) {
        return 112;
    }
    if (k == 2) {
        return 40;
    }
    if (k == 3) {
        return 26;
    }
    if (k == 4) {
        return 14;
    }
    return 0;
}

// 探索权重：1.2–5 kHz 平滑凸起，肩部到 0.8/6.2 kHz。不是 4 kHz 单点。
// / Explore weight: smooth 1.2–5 kHz bump, shoulders at 0.8/6.2 kHz. Not a 4 kHz spike.
static int32_t enhance_ex_w(uint32_t f_hz) {
    if (f_hz <= 800u || f_hz >= 6200u) {
        return 0;
    }
    if (f_hz >= 1200u && f_hz <= 5000u) {
        uint32_t x = (f_hz - 1200u) * 256u / 3800u;
        if (x > 256u) {
            x = 256u;
        }
        uint32_t bump = (4u * x * (256u - x)) / 256u;
        return (int32_t)((140u * bump) / 256u);
    }
    if (f_hz < 1200u) {
        return (int32_t)(((f_hz - 800u) * 22u) / 400u);
    }
    return (int32_t)(((6200u - f_hz) * 16u) / 1200u);
}

static void voice_fill_enhance(voice_t *v) {
    int32_t id_raw[HARM_MAX];
    int32_t ex_raw[HARM_MAX];
    int n = 0;
    int32_t id_sum = 0;
    int best = 0;
    int32_t best_w = -1;

    for (unsigned k = 1; k <= HARM_MAX; k++) {
        if (!harm_ok(v->inc, k)) {
            break;
        }
        uint32_t f_hz = (uint32_t)(((uint64_t)v->inc * (uint64_t)k * (uint64_t)SR) >> 32);
        id_raw[n] = enhance_id_w(k);
        ex_raw[n] = enhance_ex_w(f_hz);
        id_sum += id_raw[n];
        if (ex_raw[n] > best_w) {
            best_w = ex_raw[n];
            best = n;
        }
        n++;
    }
    if (n <= 0 || id_sum <= 0) {
        v->n_harm = 1;
        v->hw[0] = ENHANCE_WSUM;
        return;
    }

    // 低音只保留一段连续探索谐波，避免 20 个分量摊薄能量。
    // / Bass keeps one contiguous explore-harmonic span so 20 partials do not dilute energy.
    int ex0 = best - (ENHANCE_EX_SPAN / 2 - 1);
    int ex1 = ex0 + ENHANCE_EX_SPAN - 1;
    if (ex0 < 0) {
        ex1 -= ex0;
        ex0 = 0;
    }
    if (ex1 >= n) {
        ex0 -= (ex1 - (n - 1));
        ex1 = n - 1;
        if (ex0 < 0) {
            ex0 = 0;
        }
    }

    int32_t ex_sum = 0;
    for (int i = 0; i < n; i++) {
        if (i < ex0 || i > ex1) {
            ex_raw[i] = 0;
        }
        ex_sum += ex_raw[i];
    }

    int32_t assigned = 0;
    v->n_harm = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        int32_t w = id_raw[i] * ENHANCE_ID_MIX / id_sum;
        if (ex_sum > 0) {
            w += ex_raw[i] * ENHANCE_EX_MIX / ex_sum;
        } else if (i == 0) {
            w += ENHANCE_EX_MIX;
        }
        v->hw[i] = (uint16_t)w;
        assigned += w;
    }
    while (n > 1 && v->hw[n - 1] == 0) {
        n--;
    }
    v->n_harm = (uint8_t)n;
    if (assigned < ENHANCE_WSUM) {
        v->hw[0] = (uint16_t)(v->hw[0] + (uint16_t)(ENHANCE_WSUM - assigned));
    }
    if (v->hw[0] == 0) {
        v->hw[0] = 1;
    }
}

static void voice_fill_bright(voice_t *v) {
    int32_t raw[HARM_MAX];
    int n = 0;
    int32_t sum = 0;
    for (unsigned k = 1; k <= 7; k += 2) {
        if (!harm_ok(v->inc, k)) {
            break;
        }
        while ((int)k > n) {
            raw[n] = 0;
            n++;
        }
        raw[n - 1] = (int32_t)(256u / k);
        sum += raw[n - 1];
    }
    if (n <= 0 || sum <= 0) {
        v->n_harm = 1;
        v->hw[0] = ENHANCE_WSUM;
        return;
    }
    int32_t assigned = 0;
    v->n_harm = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        int32_t w = raw[i] * ENHANCE_WSUM / sum;
        v->hw[i] = (uint16_t)w;
        assigned += w;
    }
    if (assigned < ENHANCE_WSUM) {
        v->hw[0] = (uint16_t)(v->hw[0] + (uint16_t)(ENHANCE_WSUM - assigned));
    }
    if (v->hw[0] == 0) {
        v->hw[0] = 1;
    }
}

static int32_t wave_enhance(const voice_t *v) {
    int32_t acc = 0;
    unsigned n = v->n_harm;
    uint32_t ph = v->phase;
    for (unsigned k = 1; k <= n; k++) {
        acc += (int32_t)s_sin[(ph * (uint32_t)k) >> 24] * (int32_t)v->hw[k - 1];
    }
    return acc / ENHANCE_WSUM;
}

static int32_t voice_wave(const voice_t *v) {
    if (s_timbre == BUZZER_SCORE_TIMBRE_ENHANCE || s_timbre == BUZZER_SCORE_TIMBRE_BRIGHT) {
        return wave_enhance(v);
    }
    return wave_at(v->phase, v->inc, &k_soft);
}

void buzzer_score_set_timbre(buzzer_score_timbre_t timbre) {
    if (timbre > BUZZER_SCORE_TIMBRE_ENHANCE) {
        timbre = BUZZER_SCORE_TIMBRE_SOFT;
    }
    s_timbre = timbre;
}

buzzer_score_timbre_t buzzer_score_timbre(void) {
    return s_timbre;
}

void buzzer_score_set_staff_mask(uint8_t mask) {
    mask &= BUZZER_SCORE_STAFF_BOTH;
    s_staff_mask = mask == 0 ? BUZZER_SCORE_STAFF_BOTH : mask;
}

uint8_t buzzer_score_staff_mask(void) {
    return s_staff_mask;
}

void buzzer_score_set_semitone_shift(int8_t semitones) {
    if (semitones > 24) {
        semitones = 24;
    } else if (semitones < -24) {
        semitones = -24;
    }
    s_shift = semitones;
}

int8_t buzzer_score_semitone_shift(void) {
    return s_shift;
}

void buzzer_score_set_env(buzzer_score_env_t env) {
    s_env = env == BUZZER_SCORE_ENV_HOLD ? BUZZER_SCORE_ENV_HOLD : BUZZER_SCORE_ENV_PIANO;
}

buzzer_score_env_t buzzer_score_env(void) {
    return s_env;
}

int32_t buzzer_score_raw_peak(void) {
    return s_raw_peak;
}

int32_t buzzer_score_mix_gain_q12(void) {
    return s_mix_gain_q;
}

int32_t buzzer_score_prepare(const buzzer_duty_map_t *map, const volatile bool *abort) {
    static int16_t discard[256];
    int32_t target = buzzer_duty_legal_pcm(map);
    s_gain_enable = 0;
    s_raw_peak = 0;
    s_mix_gain_q = 1 << MIX_GAIN_SHIFT;
    buzzer_score_reset(NULL);
    while (buzzer_score_render(discard, 256, NULL)) {
        if (abort != NULL && *abort) {
            s_mix_gain_q = 1 << MIX_GAIN_SHIFT;
            s_gain_enable = 1;
            buzzer_score_reset(NULL);
            return 0;
        }
    }
    if (s_raw_peak > 0 && target > 0) {
        s_mix_gain_q = (int32_t)(((int64_t)target << MIX_GAIN_SHIFT) / (int64_t)s_raw_peak);
        if (s_mix_gain_q < 1) {
            s_mix_gain_q = 1;
        }
    } else {
        s_mix_gain_q = 0;
    }
    s_gain_enable = 1;
    buzzer_score_reset(NULL);
    return s_mix_gain_q;
}

void buzzer_score_set_harmonics(const buzzer_score_harmonics_t *h) {
    if (h != NULL) {
        s_bright = *h;
    }
}

void buzzer_score_get_harmonics(buzzer_score_harmonics_t *h) {
    if (h != NULL) {
        *h = s_bright;
    }
}

uint32_t buzzer_score_midi_inc(uint8_t midi) {
    return s_inc[midi];
}

void buzzer_score_render_hold(
    int16_t *dst, int n, uint32_t *phase, uint32_t inc, const buzzer_score_harmonics_t *h,
    int32_t amp
) {
    uint32_t ph = phase != NULL ? *phase : 0;
    const buzzer_score_harmonics_t *use = h != NULL ? h : active_harmonics();
    if (amp < 0) {
        amp = 0;
    }
    if (amp > 32767) {
        amp = 32767;
    }
    for (int i = 0; i < n; i++) {
        int32_t s = wave_at(ph, inc, use);
        dst[i] = (int16_t)((s * amp) >> 15);
        ph += inc;
    }
    if (phase != NULL) {
        *phase = ph;
    }
}

uint32_t buzzer_score_pcm_rms(const buzzer_score_stats_t *st) {
    if (st == NULL || st->samples == 0) {
        return 0;
    }
    return buzzer_u64_isqrt(st->sum_sq / (uint64_t)st->samples);
}

static uint32_t rel_samples(void) {
    return s_env == BUZZER_SCORE_ENV_HOLD ? (uint32_t)HOLD_REL_N : (uint32_t)REL_N;
}

static int32_t env_on(uint32_t age) {
    if (s_env == BUZZER_SCORE_ENV_HOLD) {
        if (age < (uint32_t)HOLD_ATK_N) {
            return (int32_t)(age * 32767u / (uint32_t)(HOLD_ATK_N ? HOLD_ATK_N : 1));
        }
        uint32_t u = age - (uint32_t)HOLD_ATK_N;
        if (u < (uint32_t)HOLD_DEC_N) {
            int32_t drop = 32767 - HOLD_SUS;
            return 32767 - (int32_t)((uint32_t)drop * u / (uint32_t)HOLD_DEC_N);
        }
        return HOLD_SUS;
    }
    if (age < (uint32_t)ATK_N) {
        return (int32_t)(age * 32767u / (uint32_t)(ATK_N ? ATK_N : 1));
    }
    uint32_t u = age - (uint32_t)ATK_N;
    if (u < (uint32_t)DEC_N) {
        int32_t drop = 32767 - SUS_ENV;
        return 32767 - (int32_t)((uint32_t)drop * u / (uint32_t)DEC_N);
    }
    return SUS_ENV;
}

static void voice_start(voice_t *v, const ev_t *ev) {
    int midi = (int)ev->midi + (int)s_shift;
    if (midi < 0) {
        midi = 0;
    } else if (midi > 127) {
        midi = 127;
    }
    v->id = ev->id;
    v->midi = (uint8_t)midi;
    v->vel = ev->vel;
    v->stage = ST_ON;
    v->phase = 0;
    v->inc = s_inc[v->midi];
    v->age = 0;
    v->rel_age = 0;
    v->env = 0;
    v->rel_from = 0;
    if (s_timbre == BUZZER_SCORE_TIMBRE_ENHANCE) {
        voice_fill_enhance(v);
    } else if (s_timbre == BUZZER_SCORE_TIMBRE_BRIGHT) {
        voice_fill_bright(v);
    } else {
        v->n_harm = 0;
    }
}

static void voice_release(voice_t *v) {
    if (v->stage == ST_OFF) {
        return;
    }
    v->stage = ST_REL;
    v->rel_from = v->env;
    v->rel_age = 0;
}

static voice_t *find_id(uint16_t id) {
    for (int i = 0; i < NVOICE; i++) {
        if (s_voice[i].stage != ST_OFF && s_voice[i].id == id) {
            return &s_voice[i];
        }
    }
    return NULL;
}

static voice_t *alloc_voice(void) {
    for (int i = 0; i < NVOICE; i++) {
        if (s_voice[i].stage == ST_OFF) {
            return &s_voice[i];
        }
    }
    voice_t *best = NULL;
    for (int i = 0; i < NVOICE; i++) {
        if (s_voice[i].stage != ST_REL) {
            continue;
        }
        if (best == NULL || s_voice[i].env < best->env) {
            best = &s_voice[i];
        }
    }
    if (best != NULL) {
        return best;
    }
    best = &s_voice[0];
    for (int i = 1; i < NVOICE; i++) {
        if (s_voice[i].age > best->age) {
            best = &s_voice[i];
        }
    }
    return best;
}

static void apply_event(const ev_t *ev, buzzer_score_stats_t *st) {
    if (!ev->on) {
        voice_t *v = find_id(ev->id);
        if (v != NULL) {
            voice_release(v);
            if (st != NULL) {
                st->note_offs++;
            }
        }
        return;
    }
    if (((1u << ev->staff) & s_staff_mask) == 0) {
        return;
    }
    voice_t *v = find_id(ev->id);
    if (v == NULL) {
        v = alloc_voice();
    }
    voice_start(v, ev);
    if (st != NULL) {
        st->note_ons++;
    }
}

static void apply_due(buzzer_score_stats_t *st) {
    while (s_evi < s_nev && s_ev[s_evi].sample == s_pos) {
        apply_event(&s_ev[s_evi], st);
        s_evi++;
    }
}

static int32_t step_voice(voice_t *v) {
    int32_t s = voice_wave(v);
    v->phase += v->inc;
    if (v->stage == ST_ON) {
        v->env = env_on(v->age);
        v->age++;
    } else {
        v->rel_age++;
        uint32_t rn = rel_samples();
        if (v->rel_age >= rn || v->rel_from <= 0) {
            v->env = 0;
            v->stage = ST_OFF;
            v->id = 0;
            return 0;
        }
        v->env = (int32_t)((int64_t)v->rel_from * ((int32_t)rn - (int32_t)v->rel_age) / (int32_t)rn);
    }
    int32_t v16 = (int32_t)(((int64_t)s * v->env) >> 15);
    return (v16 * (int32_t)v->vel) >> 8;
}

bool buzzer_score_render(int16_t *dst, int n, buzzer_score_stats_t *st) {
    if (s_done || n <= 0) {
        if (n > 0) {
            memset(dst, 0, (size_t)n * sizeof(int16_t));
        }
        return false;
    }

    const uint32_t body = buzzer_score_body_samples();
    for (int i = 0; i < n; i++) {
        apply_due(st);

        int32_t mix = 0;
        uint32_t live = 0;
        for (int v = 0; v < NVOICE; v++) {
            if (s_voice[v].stage == ST_OFF) {
                continue;
            }
            mix += step_voice(&s_voice[v]);
            if (s_voice[v].stage != ST_OFF) {
                live++;
            }
        }

        int32_t raw_abs = mix < 0 ? -mix : mix;
        if (!s_gain_enable && raw_abs > s_raw_peak) {
            s_raw_peak = raw_abs;
        }
        if (s_gain_enable && s_mix_gain_q != (1 << MIX_GAIN_SHIFT)) {
            mix = (int32_t)(((int64_t)mix * (int64_t)s_mix_gain_q) >> MIX_GAIN_SHIFT);
        }

        if (st != NULL) {
            int32_t abs = mix < 0 ? -mix : mix;
            if (abs > st->peak) {
                st->peak = abs;
            }
            if (live > st->max_voices) {
                st->max_voices = live;
            }
            st->samples++;
        }
        if (mix > 32767) {
            mix = 32767;
            if (st != NULL) {
                st->clips++;
            }
        } else if (mix < -32768) {
            mix = -32768;
            if (st != NULL) {
                st->clips++;
            }
        }
        if (st != NULL) {
            st->sum_sq += (uint64_t)((int64_t)mix * (int64_t)mix);
        }
        dst[i] = (int16_t)mix;
        s_pos++;

        if (s_pos >= SAMPLE_CAP) {
            s_done = true;
            if (i + 1 < n) {
                memset(dst + i + 1, 0, (size_t)(n - i - 1) * sizeof(int16_t));
            }
            return true;
        }
    }

    if (s_evi >= s_nev && s_pos >= body) {
        bool live = false;
        for (int v = 0; v < NVOICE; v++) {
            if (s_voice[v].stage != ST_OFF) {
                live = true;
                break;
            }
        }
        if (!live) {
            s_done = true;
        }
    }
    return true;
}
