/* SPDX-License-Identifier: MIT */

#ifndef HV_VGIC_DIAG_H
#define HV_VGIC_DIAG_H

#ifdef HV_VGIC_DIAG_HOST_TEST
#include <stdint.h>
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include "types.h"
#endif

#define HV_VGIC_DIAG_LR_COUNT 8u

struct hv_vgic_diag_snapshot {
    u32 pending_lrs;
    u32 active_lrs;
    u32 occupied_lrs;
};

void hv_vgic_diag_classify_lrs(const u64 lrs[HV_VGIC_DIAG_LR_COUNT],
                                struct hv_vgic_diag_snapshot *out);

#endif
