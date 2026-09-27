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
    assert(!book_shake_feed(&g, true, false, 1000));
    assert(!book_shake_feed(&g, true, false, 1040));
    assert(!book_shake_feed(&g, false, false, 1080));
    assert(book_shake_feed(&g, true, false, 1500));
    assert(!book_shake_feed(&g, false, false, 1600));
    assert(!book_shake_feed(&g, true, false, 1700));
    assert(!book_shake_feed(&g, false, false, 3100));
    assert(!book_shake_feed(&g, true, true, 3200));
    assert(!book_shake_feed(&g, true, false, 3300));
    assert(!book_shake_feed(&g, false, false, 3400));
    assert(!book_shake_feed(&g, true, false, 3500));
    assert(!book_shake_feed(&g, false, false, 4000));
    assert(!book_shake_feed(&g, true, false, 4200));
    assert(!book_shake_feed(&g, false, false, 4240));
    assert(book_shake_feed(&g, true, false, 4300));
    g = (book_shake_gate_t){0};
    assert(!book_shake_feed(&g, true, false, 1000));
    assert(!book_shake_feed(&g, false, false, 1100));
    assert(book_shake_feed(&g, true, false, 1500));
    assert(!book_shake_feed(&g, false, false, 2260));
    assert(!book_shake_feed(&g, true, false, 2300));
    assert(!book_shake_feed(&g, false, false, 2340));
    assert(book_shake_feed(&g, true, false, 2380));
    puts("PASS book position and shake gates");
}
