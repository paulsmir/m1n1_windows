/* SPDX-License-Identifier: MIT */

#include "dcp_iomfb_owner_lifecycle.h"

bool dcp_iomfb_owner_start_ordered(void *opaque,
                                   dcp_iomfb_owner_stage_fn start_system,
                                   dcp_iomfb_owner_stage_fn start_iomfb)
{
    if (!start_system || !start_iomfb || !start_system(opaque))
        return false;
    return start_iomfb(opaque);
}
