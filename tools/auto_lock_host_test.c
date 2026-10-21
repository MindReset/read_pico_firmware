/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "auto_lock.h"
int main(void) {
    const int64_t input = INT64_C(1000000);
    for (unsigned minutes = 1; minutes <= 10; minutes += minutes == 1 ? 4 : 5) {
        const int64_t deadline = input + (int64_t)minutes * 60000;
        assert(!auto_lock_due(deadline - 1, input, minutes));
        assert(auto_lock_due(deadline, input, minutes));
        assert(auto_lock_due(deadline + 1, input, minutes));
        assert(!auto_lock_due(deadline, deadline, minutes)); // User input resets the interval.
        assert(!auto_lock_due(input - 1, input, minutes));
    }
    assert(!auto_lock_due(INT64_C(1000000000000), input, 0));
    assert(!auto_lock_due(INT64_C(1000000000000), input, 2));
    assert(!auto_lock_due(INT64_C(1000000000000), input, 255));
    puts("auto lock: off, supported intervals, boundary and input reset passed");
}
