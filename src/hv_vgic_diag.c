/* SPDX-License-Identifier: MIT */

#include "hv_vgic_diag.h"

void hv_vgic_diag_classify_lrs(const u64 lrs[HV_VGIC_DIAG_LR_COUNT],
                                struct hv_vgic_diag_snapshot *out)
{
    if (!out)
        return;

    *out = (struct hv_vgic_diag_snapshot){0};
    if (!lrs)
        return;

    for (u32 i = 0; i < HV_VGIC_DIAG_LR_COUNT; i++) {
        u32 state = (lrs[i] >> 62) & 3;
        if (state & 1)
            out->pending_lrs++;
        if (state & 2)
            out->active_lrs++;
        if (state)
            out->occupied_lrs++;
    }
}
