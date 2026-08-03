/* SPDX-License-Identifier: MIT */

#include "hv_irq_routes.h"

#define ROUTE_COUNT (sizeof(routes) / sizeof(routes[0]))

/*
 * Physical AIC numbers and guest GIC INTIDs are separate namespaces. Keep the
 * handoff explicit even when a route currently uses the same number in both.
 * usb-drd1 is the Type-C port not occupied by the m1n1 proxy on J313.
 */
static const struct hv_irq_route routes[] = {
    {.hw_irq = 857, .vintid = 857, .level = true},
};

const struct hv_irq_route *hv_irq_route_from_hw(u32 hw_irq)
{
    for (u32 i = 0; i < ROUTE_COUNT; i++) {
        if (routes[i].hw_irq == hw_irq)
            return &routes[i];
    }

    return NULL;
}

const struct hv_irq_route *hv_irq_route_from_vintid(u32 vintid)
{
    for (u32 i = 0; i < ROUTE_COUNT; i++) {
        if (routes[i].vintid == vintid)
            return &routes[i];
    }

    return NULL;
}

bool hv_irq_route_resolve_incoming(u32 hw_irq, u32 reserved_vintid, u32 *vintid)
{
    const struct hv_irq_route *route = hv_irq_route_from_hw(hw_irq);
    u32 candidate = route ? route->vintid : hw_irq;

    if (!vintid || candidate == reserved_vintid)
        return false;

    *vintid = candidate;
    return true;
}

bool hv_irq_route_level_eoi_target(u32 vintid, bool enabled, u32 *hw_irq)
{
    const struct hv_irq_route *route = hv_irq_route_from_vintid(vintid);

    if (!route || !route->level || !enabled || !hw_irq)
        return false;

    *hw_irq = route->hw_irq;
    return true;
}
