// SPDX-License-Identifier: MIT
#include "hv_tick_policy.h"

#define HV_ECV_SECONDARY_TICK_RATE 1
#define HV_FALLBACK_SECONDARY_TICK_RATE 100
#define HV_RUNTIME_TICK_RATE       100
#define HV_GUEST_IRQ_RECOVERY_TICK_RATE 0

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

uint32_t hv_runtime_tick_rate(void)
{
    return HV_RUNTIME_TICK_RATE;
}

uint32_t hv_guest_irq_recovery_tick_rate(void)
{
    /*
     * The separate 1 ms recovery source was added after the accepted responsive
     * baseline.  Hardware proved that it fires, but not that it repairs guest
     * timer delivery; instead it caused near-continuous EL2/vGIC work.  Keep the
     * API for launch-contract compatibility while disabling that source.  The
     * ordinary 100 Hz secondary heartbeat remains the bounded fallback.
     */
    return HV_GUEST_IRQ_RECOVERY_TICK_RATE;
}

uint32_t hv_secondary_tick_rate(bool has_ecv)
{
    /*
     * ECV lets secondaries run with an almost idle EL2 housekeeping tick. T8103
     * has no ECV. Its guest timer normally arrives on its own FIQ route, but
     * hardware snapshots have shown P-cluster timer LRs stuck active+pending with
     * the physical route masked. The housekeeping tick remains a bounded recovery
     * path, but it is not the guest architectural timer. Running it at 1 kHz on
     * every secondary causes seven thousand extra EL2 entries and vGIC
     * resynchronisations per second. Keep the 10 ms recovery bound used by the
     * accepted responsive baseline; normal timer delivery remains event driven.
     */
    return has_ecv ? HV_ECV_SECONDARY_TICK_RATE : HV_FALLBACK_SECONDARY_TICK_RATE;
}

uint64_t hv_tick_interval_ticks(uint64_t counter_frequency, uint32_t tick_rate)
{
    if (!tick_rate)
        return 0;
    return counter_frequency / tick_rate;
}
