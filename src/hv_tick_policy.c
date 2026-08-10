// SPDX-License-Identifier: MIT
#include "hv_tick_policy.h"

#define HV_ECV_SECONDARY_TICK_RATE 1
#define HV_FALLBACK_SECONDARY_TICK_RATE 100

uint32_t hv_secondary_tick_rate(bool has_ecv)
{
    /*
     * ECV lets secondaries run with an almost idle EL2 housekeeping tick.  T8103
     * has no ECV, but driving the 5 kHz service cadence on all eight CPUs causes
     * roughly 40,000 EL2 entries per second.  The guest timer has its own FIQ;
     * 100 Hz remains only as a bounded fallback for lost-delivery recovery and
     * diagnostics while avoiding the pathological all-core polling overhead.
     */
    return has_ecv ? HV_ECV_SECONDARY_TICK_RATE : HV_FALLBACK_SECONDARY_TICK_RATE;
}

uint64_t hv_tick_interval_ticks(uint64_t counter_frequency, uint32_t tick_rate)
{
    if (!tick_rate)
        return 0;
    return counter_frequency / tick_rate;
}
