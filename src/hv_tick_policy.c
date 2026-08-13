// SPDX-License-Identifier: MIT
#include "hv_tick_policy.h"

#define HV_ECV_SECONDARY_TICK_RATE 1
#define HV_FALLBACK_SECONDARY_TICK_RATE 1000

uint32_t hv_boot_tick_rate(void)
{
    /*
     * Guest architectural timers are delivered by their own FIQ routes.  The
     * boot-CPU tick only services host-side proxy, UART and diagnostic work;
     * polling all of that at 5 kHz steals a measurable fraction of CPU0 from
     * Windows.  One millisecond keeps the debug transport responsive without
     * turning the monitor itself into a scheduler load.
     */
    return 1000;
}

uint32_t hv_secondary_tick_rate(bool has_ecv)
{
    /*
     * ECV lets secondaries run with an almost idle EL2 housekeeping tick. T8103
     * has no ECV. Its guest timer normally arrives on its own FIQ route, but
     * hardware snapshots have repeatedly shown P-cluster timer LRs stuck
     * active+pending with the physical route masked. The housekeeping tick is the
     * bounded recovery path for that lost progress. Use a 1 ms recovery bound: the
     * previous 10 ms bound allowed CPU4/CPU6 to remain stalled until Windows fired
     * CLOCK_WATCHDOG_TIMEOUT. This is still one fifth of the old 5 kHz cadence and
     * avoids restoring the pathological all-core polling overhead.
     */
    return has_ecv ? HV_ECV_SECONDARY_TICK_RATE : HV_FALLBACK_SECONDARY_TICK_RATE;
}

uint64_t hv_tick_interval_ticks(uint64_t counter_frequency, uint32_t tick_rate)
{
    if (!tick_rate)
        return 0;
    return counter_frequency / tick_rate;
}
