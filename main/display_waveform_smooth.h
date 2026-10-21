/* SPDX-License-Identifier: Apache-2.0
 * 中文：只交错最多两对反向脉冲，不改相数、动作数量、保持相或首尾驱动。
 * English: Interleave at most two opposite pulse pairs; preserve phase/action counts, holds and endpoints.
 */
#pragma once
#include "e0470_epaper_waveform.h"
static inline void display_soften_gray_pair(uint8_t *data,const EpdWaveformPhases *original,int to,int from) {
    const int byte=from/4,shift=2*(3-from%4);
    int first = -1, end = -1, prefix_action = 0, prefix_end = 0, prefix_count = 0;
    for (int phase = 0; phase < original->phases; ++phase) {
        int action = e0470_phase_action(original, phase, to, from);
        if (action && first < 0) { first = phase; prefix_action = action; }
        if (action) end = phase;
        if (first >= 0 && action && action != prefix_action) { prefix_end = phase; break; }
        if (action) ++prefix_count;
    }
    // last 表示原最后主动相，首尾都不交换。/ Preserve both the first and final active phase.
    for (int phase = original->phases - 1; phase >= 0; --phase)
        if (e0470_phase_action(original, phase, to, from)) { end = phase; break; }
    if (!prefix_end || prefix_count < 5 || (prefix_action != 1 && prefix_action != 2)) return;
    int moved = 0, opposite = prefix_end;
    for (int phase = prefix_end - 1; phase > first && moved < 2; --phase) {
        if (e0470_phase_action(original, phase, to, from) != prefix_action) continue;
        while (opposite < end && e0470_phase_action(original, opposite, to, from) != 3 - prefix_action) ++opposite;
        if (opposite >= end) break;
        data[(phase*16+to)*4+byte] = (uint8_t)((data[(phase*16+to)*4+byte] & ~(3u << shift)) | ((3 - prefix_action) << shift));
        data[(opposite*16+to)*4+byte] = (uint8_t)((data[(opposite*16+to)*4+byte] & ~(3u << shift)) | (prefix_action << shift));
        ++opposite; ++moved;
        // 两对交换隔开一相，避免将峰值平移为另一个突变。/ Separate the swaps rather than merely shifting a peak.
        --phase;
    }
}
