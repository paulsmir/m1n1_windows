/* SPDX-License-Identifier: MIT */

#include "dcp_iomfb_clock.h"

bool dcp_iomfb_clock_now(const struct dcp_iomfb_clock_anchor *anchor,
                         u64 counter, u64 frequency, u64 *utc_ms)
{
    u64 delta;
    u64 seconds;
    u64 millis;

    if (!anchor || !utc_ms || !frequency ||
        anchor->frequency != frequency || counter < anchor->counter)
        return false;
    delta = counter - anchor->counter;
    seconds = delta / frequency;
    if (seconds > (~(u64)0 - anchor->utc_ms) / 1000)
        return false;
    millis = anchor->utc_ms + seconds * 1000;
    delta %= frequency;
    if (delta > ~(u64)0 / 1000)
        return false;
    millis += delta * 1000 / frequency;
    if (millis < anchor->utc_ms)
        return false;
    *utc_ms = millis;
    return true;
}
