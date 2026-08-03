/* SPDX-License-Identifier: MIT */

#ifndef HV_IRQ_ROUTES_H
#define HV_IRQ_ROUTES_H

#ifdef HV_IRQ_ROUTES_HOST_TEST
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint32_t u32;
#else
#include "types.h"
#endif

struct hv_irq_route {
    u32 hw_irq;
    u32 vintid;
    bool level;
};

const struct hv_irq_route *hv_irq_route_from_hw(u32 hw_irq);
const struct hv_irq_route *hv_irq_route_from_vintid(u32 vintid);
bool hv_irq_route_resolve_incoming(u32 hw_irq, u32 reserved_vintid, u32 *vintid);
bool hv_irq_route_level_eoi_target(u32 vintid, bool enabled, u32 *hw_irq);

#endif
