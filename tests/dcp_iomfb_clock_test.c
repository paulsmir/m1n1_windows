/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/dcp_iomfb_clock.c"

int main(void)
{
    struct dcp_iomfb_clock_anchor anchor = {
        .utc_ms = 1725148800000ULL,
        .counter = 24000000ULL,
        .frequency = 24000000ULL,
    };
    u64 utc = 0;

    assert(dcp_iomfb_clock_now(&anchor, 60000000ULL, 24000000ULL, &utc));
    assert(utc == anchor.utc_ms + 1500ULL);
    assert(!dcp_iomfb_clock_now(&anchor, anchor.counter - 1ULL,
                                anchor.frequency, &utc));
    assert(!dcp_iomfb_clock_now(&anchor, anchor.counter,
                                anchor.frequency + 1ULL, &utc));
    anchor.utc_ms = UINT64_MAX - 500ULL;
    assert(!dcp_iomfb_clock_now(&anchor,
                                anchor.counter + anchor.frequency,
                                anchor.frequency, &utc));
    puts("dcp_iomfb_clock_test: PASS");
    return 0;
}
