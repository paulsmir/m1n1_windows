#include <assert.h>
#include <stdio.h>

#include "../src/hv_launch_golden_j313.h"

int main(void)
{
    struct hv_contract_snapshot golden[HV_LAUNCH_PREFLIGHT_BLOCKING_CHECKPOINTS];

    assert(hv_launch_golden_j313_init(golden));
    for (unsigned int i = 0; i < HV_LAUNCH_PREFLIGHT_BLOCKING_CHECKPOINTS; i++) {
        assert(golden[i].header.checkpoint == i);
        assert(golden[i].header.sequence == i + 1);
        assert(golden[i].boot.guest_entry == 0x8510b4000ULL);
        assert(golden[i].boot.args[0] == 0x8533e8000ULL);
        assert(golden[i].cpu_count == 8);
        assert(golden[i].cpus[7].mpidr == 0x10103ULL);
    }
    assert(golden[0].cpus[1].actlr == 0xc00);
    assert(golden[1].cpus[1].actlr == 0x1c00);
    assert(golden[3].cpus[1].hacr == 0x317000000014000ULL);
    assert(golden[3].irq_routes[0].physical_irq == 857);

    puts("hv_launch_golden_j313_test: ok");
    return 0;
}
