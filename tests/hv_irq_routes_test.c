#include <assert.h>
#include <stdio.h>

#include "../src/hv_irq_routes.h"

int main(void)
{
    const struct hv_irq_route *route;
    u32 hw_irq = 0;
    u32 vintid = 0;

    route = hv_irq_route_from_hw(857);
    assert(route != NULL);
    assert(route->hw_irq == 857);
    assert(route->vintid == 857);
    assert(route->level);

    assert(hv_irq_route_from_vintid(857) == route);
    assert(hv_irq_route_from_hw(64) == NULL);
    assert(hv_irq_route_from_vintid(64) == NULL);

    /* Guest EOIs may only unmask a physical line through an explicit route. */
    assert(!hv_irq_route_level_eoi_target(64, true, &hw_irq));
    assert(!hv_irq_route_level_eoi_target(857, false, &hw_irq));
    assert(hv_irq_route_level_eoi_target(857, true, &hw_irq));
    assert(hw_irq == 857);

    /* A raw AIC IRQ must not alias a synthetic guest-only interrupt. */
    assert(!hv_irq_route_resolve_incoming(64, 64, &vintid));
    assert(hv_irq_route_resolve_incoming(123, 64, &vintid));
    assert(vintid == 123);
    assert(hv_irq_route_resolve_incoming(857, 64, &vintid));
    assert(vintid == 857);

    puts("hv_irq_routes_test: ok");
    return 0;
}
