#include <assert.h>
#include <stdint.h>

#include "../src/hv_tick_policy.h"

int main(void)
{
    assert(hv_boot_tick_rate() == 1000);
    assert(hv_secondary_tick_rate(true) == 1);
    assert(hv_secondary_tick_rate(false) == 1000);

    assert(hv_tick_interval_ticks(24000000, hv_boot_tick_rate()) == 24000);
    assert(hv_tick_interval_ticks(24000000, hv_secondary_tick_rate(false)) == 24000);
    return 0;
}
