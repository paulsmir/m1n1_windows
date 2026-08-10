#include <assert.h>
#include <stdint.h>

#include "../src/hv_tick_policy.h"

int main(void)
{
    assert(hv_secondary_tick_rate(true) == 1);
    assert(hv_secondary_tick_rate(false) == 100);

    assert(hv_tick_interval_ticks(24000000, 5000) == 4800);
    assert(hv_tick_interval_ticks(24000000, hv_secondary_tick_rate(false)) == 240000);
    return 0;
}
