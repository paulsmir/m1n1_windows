#include <assert.h>
#include <stdint.h>

#include "../src/hv_tick_policy.h"

int main(void)
{
    /* Match the accepted responsive baseline on CPU0.  Only secondaries use
     * the sparse fallback cadence; lowering CPU0 to 100 Hz produced measured
     * tens-of-seconds guest progress pauses at the Windows lock screen. */
    assert(hv_boot_tick_rate() == 5000);
    assert(hv_runtime_tick_rate() == 5000);
    /* The accepted responsive path has no second 1 ms recovery source.  The
     * ordinary sparse secondary heartbeat is the only bounded fallback. */
    assert(hv_guest_irq_recovery_tick_rate() == 0);
    assert(hv_secondary_tick_rate(true) == 1);
    /* T8103 has no ECV.  Its secondary tick is a lost-delivery recovery
     * fallback, not the guest architectural timer, so it must stay sparse
     * enough to avoid continuous EL2/vGIC work on all seven secondaries. */
    assert(hv_secondary_tick_rate(false) == 100);

    assert(hv_tick_interval_ticks(24000000, hv_boot_tick_rate()) == 4800);
    assert(hv_tick_interval_ticks(24000000, hv_runtime_tick_rate()) == 4800);
    assert(hv_tick_interval_ticks(24000000, hv_guest_irq_recovery_tick_rate()) == 0);
    assert(hv_tick_interval_ticks(24000000, hv_secondary_tick_rate(false)) == 240000);
    return 0;
}
