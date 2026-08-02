#include <assert.h>
#include <stdio.h>

#include "../src/hv_irq_routes.h"

int main(void)
{
    const struct hv_irq_route *route;
    u32 hw_irq = 0;

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

    puts("hv_irq_routes_test: ok");
    return 0;
}
