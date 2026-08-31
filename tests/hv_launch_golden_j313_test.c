#include <assert.h>
#include <stdio.h>

#include "../src/hv_apple_input.generated.h"
#include "../src/hv_launch_golden_j313.h"

static void assert_agx_routes(const struct hv_contract_snapshot *snapshot)
{
    static const struct hv_contract_irq_route expected[] = {
        {.physical_irq = 563, .vintid = 880, .flags = HV_CONTRACT_IRQ_LEVEL},
        {.physical_irq = 564, .vintid = 881, .flags = HV_CONTRACT_IRQ_LEVEL},
        {.physical_irq = 565, .vintid = 882, .flags = HV_CONTRACT_IRQ_LEVEL},
        {.physical_irq = 566, .vintid = 883, .flags = HV_CONTRACT_IRQ_LEVEL},
        {.physical_irq = 579, .vintid = 884, .flags = HV_CONTRACT_IRQ_LEVEL},
        {.physical_irq = 576, .vintid = 885, .flags = HV_CONTRACT_IRQ_LEVEL},
        {.physical_irq = 575, .vintid = 886, .flags = HV_CONTRACT_IRQ_LEVEL},
        {.physical_irq = 578, .vintid = 887, .flags = HV_CONTRACT_IRQ_LEVEL},
        {.physical_irq = 577, .vintid = 888, .flags = HV_CONTRACT_IRQ_LEVEL},
    };

    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        const struct hv_contract_irq_route *route = &snapshot->irq_routes[i + 1];
        assert(route->physical_irq == expected[i].physical_irq);
        assert(route->vintid == expected[i].vintid);
        assert(route->flags == expected[i].flags);
    }
}

int main(void)
{
    struct hv_contract_snapshot golden[HV_LAUNCH_PREFLIGHT_BLOCKING_CHECKPOINTS];

    assert(hv_launch_golden_j313_init(golden, false));
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
    assert(golden[0].irq_route_count == 1);
    assert(golden[1].irq_route_count == 10);
    assert_agx_routes(&golden[1]);

    assert(hv_launch_golden_j313_init(golden, true));
    assert(golden[0].irq_route_count == 1);
    assert(golden[1].irq_route_count == 11);
    assert_agx_routes(&golden[1]);
    assert(golden[1].irq_routes[10].physical_irq == HV_APPLE_INPUT_PHYSICAL_PARENT_IRQ);
    assert(golden[1].irq_routes[10].vintid == HV_APPLE_INPUT_GUEST_VINTID);

    puts("hv_launch_golden_j313_test: ok");
    return 0;
}
