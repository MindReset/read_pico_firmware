/* SPDX-License-Identifier: Apache-2.0
 * 中文：实际阅读波形的驱动边界；光学灰度和残影仍需实屏验证。
 * English: Validate actual reader pulse boundaries; optical tone and ghosting need panel tests.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "display_reader_waveform.h"
#include "e0470_epaper_waveform.h"

int main(void) {
    e0470_waveform_init();
    const EpdWaveformPhases* old = e0470_waveform_phases(&E0470_TEXTTURN_WAVEFORM, MODE_GL16);
    uint8_t saved[E0470_GL16_FRAMES * 16 * 4];
    assert(old && old->phases == E0470_GL16_FRAMES && !old->phase_times);
    memcpy(saved, old->luts, sizeof(saved));
    const EpdWaveform* wave = display_reader_text_waveform();
    const EpdWaveformPhases* soft = e0470_waveform_phases(wave, MODE_GL16);
    assert(wave != &E0470_TEXTTURN_WAVEFORM && display_reader_text_waveform() == wave);
    assert(wave->temp_intervals == E0470_TEXTTURN_WAVEFORM.temp_intervals);
    assert(soft && soft->phases == old->phases && soft->phase_times == old->phase_times);
    unsigned changed = 0;
    for (int from = 0; from < 16; ++from) for (int to = 0; to < 16; ++to) {
        int a_count[4] = {0}, b_count[4] = {0}, a_balance = 0, b_balance = 0;
        int a_peak = 0, b_peak = 0, first = -1, last = -1;
        unsigned differences = 0;
        for (int f = 0; f < old->phases; ++f) {
            int a = e0470_phase_action(old, f, to, from), b = e0470_phase_action(soft, f, to, from);
            ++a_count[a]; ++b_count[b];
            if (a != b) ++differences;
            if (from != 15 || to == 0 || to == 15) assert(a == b);
            if (a == 0 || a == 3) assert(b == a);
            if (a) {if (first < 0) first = f; last = f;}
            a_balance += a == 1 ? 1 : a == 2 ? -1 : 0;
            b_balance += b == 1 ? 1 : b == 2 ? -1 : 0;
            if (a_balance > a_peak) a_peak = a_balance;
            if (b_balance > b_peak) b_peak = b_balance;
        }
        assert(!memcmp(a_count, b_count, sizeof(a_count)));
        if (first >= 0) assert(e0470_phase_action(old, first, to, from) == e0470_phase_action(soft, first, to, from));
        if (last >= 0) assert(e0470_phase_action(old, last, to, from) == e0470_phase_action(soft, last, to, from));
        if (differences) {
            assert(from == 15 && to > 0 && to < 15 && differences <= 4);
            assert(b_peak < a_peak && a_peak - b_peak <= 2);
            ++changed;
            printf("15->%d net black pulse peak %d -> %d; phases ", to, a_peak, b_peak);
            for (int f = 0; f < soft->phases; ++f) putchar(".BW?"[e0470_phase_action(soft, f, to, from)]);
            putchar('\n');
        }
    }
    assert(changed > 0 && !memcmp(saved, old->luts, sizeof(saved)));
    puts("PASS: all 256 pairs, endpoints, holds, first/final actions, pulse counts, 37-frame timing and original LUT retained; optical validation pending");
    return 0;
}
