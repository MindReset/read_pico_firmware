/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 图书进度与晃动门槛测试。/ Book progress and shake-gate tests.
 */
#include <assert.h>
#include <stdio.h>
#include "book_policy.h"

int main(void) {
    assert(book_position_bytes(100, 200, 50, 100) == 150);
    assert(book_position_bytes(100, 200, 150, 100) == 200);
    assert(book_position_bytes(100, 200, 0, 0) == 100);
    assert(book_position_bytes(0, UINT32_MAX, UINT32_MAX, UINT32_MAX) == UINT32_MAX);
    book_shake_gate_t g = {0};
    // 任意持机角度均从重力基准开始，静止后一次水平动作即可翻页。
    // Any holding angle starts from gravity; one horizontal impulse after rest turns a page.
    for (int t = 1000; t <= 1140; t += 20) assert(book_shake_feed(&g, 180, -600, -760, false, t) == 0);
    assert(book_shake_feed(&g, 480, -590, -750, false, 1160) == 1);
    assert(book_shake_feed(&g, -150, -600, -760, false, 1180) == 0); /* 回弹 / Rebound */
    for (int t = 1200; t <= 2180; t += 20) assert(!book_shake_feed(&g, 180, -600, -760, false, t));
    assert(book_shake_feed(&g, -160, -595, -760, false, 2200) == -1);
    assert(!book_shake_feed(&g, 520, -600, -760, false, 2220));
    // 平滑的横向晃动也能识别，不要求采样间突然阶跃。
    // Recognize rounded horizontal gestures without requiring an abrupt sample step.
    const int smooth[] = {70, 138, 200, 255, 300, 333, 353, 360, 353, 333, 300,
                          255, 200, 138, 70, 0, -70, -138, -200, -255, -300, -333,
                          -353, -360, -353, -333, -300, -255, -200, -138, -70, 0};
    for (int sign=-1;sign<=1;sign+=2) {
        g = (book_shake_gate_t){0};
        for (int t=0;t<=160;t+=20) assert(!book_shake_feed(&g,0,0,-1000,false,t));
        int turns=0;
        for (size_t i=0;i<sizeof(smooth)/sizeof(smooth[0]);++i) {
            int dir=book_shake_feed(&g,sign*smooth[i],0,-1000,false,180+(int64_t)i*20);
            if (dir) { assert(dir==sign); ++turns; }
        }
        assert(turns==1);
    }
    // 抖动、慢倾斜、纵向或前后运动不翻页。/ Reject jitter, slow tilts and other axes.
    g = (book_shake_gate_t){0};
    for (int t = 0; t <= 500; t += 20) assert(!book_shake_feed(&g, t % 40 ? 30 : -30, 0, -1000, false, t));
    for (int t = 520; t <= 1200; t += 20) assert(!book_shake_feed(&g, (t - 520) / 2, 0, -1000, false, t));
    for (int t = 1220; t <= 1500; t += 20) assert(!book_shake_feed(&g, 340, 0, -1000, false, t));
    assert(!book_shake_feed(&g, 350, 450, -1000, false, 1520));
    assert(!book_shake_feed(&g, 0, 0, -1000, false, 1540)); /* 纵向动作后回弹 / Rebound after rejected motion */
    for (int t = 1560; t <= 1940; t += 20) assert(!book_shake_feed(&g, 340, 0, -1000, false, t));
    assert(!book_shake_feed(&g, 600, 260, -1000, false, 1960)); /* 对角 / Diagonal */
    for (int t = 1980; t <= 2360; t += 20) assert(!book_shake_feed(&g, 340, 0, -1000, false, t));
    assert(!book_shake_feed(&g, 340, 0, -500, false, 2380));
    // 触摸、工具栏或屏幕刷新的屏蔽期结束后必须先静止，不能继承半个动作。
    // Touch/tools/refresh suppression requires fresh rest afterward; do not inherit partial motion.
    assert(!book_shake_feed(&g, 640, 0, -1000, true, 2400));
    assert(!book_shake_feed(&g, 340, 0, -1000, true, 2420));
    for (int t = 2440; t <= 2580; t += 20) assert(!book_shake_feed(&g, 340, 0, -1000, false, t));
    assert(book_shake_feed(&g, 60, 0, -1000, false, 2600) == -1);
    // 丢采样、离页或时间重置后不让旧方向误触发。/ Rebaseline after lost samples, leaving or clock reset.
    assert(!book_shake_feed(&g, 700, 0, -1000, false, 4000));
    assert(!book_shake_feed(&g, -700, 0, -1000, false, 10));
    assert(!book_shake_feed(NULL, 0, 0, 0, false, 0));
    puts("PASS book position and directional shake: left/right, rebound, tilt and suppression");
}
