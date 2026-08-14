#include <assert.h>
#include <stdint.h>

#include "../src/hv_tick_policy.h"

int main(void)
{
    /* Event-driven timer delivery is authoritative on every vCPU.  The boot
     * CPU must not pay a unique 1 kHz EL2 recovery tax. */
    assert(hv_boot_tick_rate() == 100);
    assert(hv_secondary_tick_rate(true) == 1);
    /* T8103 has no ECV.  Its secondary tick is a lost-delivery recovery
     * fallback, not the guest architectural timer, so it must stay sparse
     * enough to avoid continuous EL2/vGIC work on all seven secondaries. */
    assert(hv_secondary_tick_rate(false) == 100);

    assert(hv_tick_interval_ticks(24000000, hv_boot_tick_rate()) == 240000);
    assert(hv_tick_interval_ticks(24000000, hv_secondary_tick_rate(false)) == 240000);
    return 0;
}
