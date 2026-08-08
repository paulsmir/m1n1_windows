/* SPDX-License-Identifier: MIT */

#ifndef HV_EXCEPTION_LOWER_H
#define HV_EXCEPTION_LOWER_H

#ifdef HV_EXCEPTION_LOWER_HOST_TEST
#include <stdbool.h>
#include <stdint.h>
typedef uint64_t hv_exception_u64;
#else
#include "types.h"
typedef u64 hv_exception_u64;
#endif

struct hv_exception_lower_plan {
    hv_exception_u64 target_spsr;
    hv_exception_u64 target_elr;
};

/*
 * Recreate the architectural exception entry that EL2 intercepted.  Windows uses
 * BRK instructions internally and expects them to arrive at its EL1 synchronous
 * vector.  The assisted Python hypervisor performs this same lowering operation.
 */
static inline bool hv_exception_lower_plan(hv_exception_u64 source_spsr,
                                           hv_exception_u64 vbar,
                                           struct hv_exception_lower_plan *plan)
{
    const hv_exception_u64 mode_mask = 0x1f;
    const hv_exception_u64 daif_mask = 0x3c0;
    hv_exception_u64 vector_offset;

    switch (source_spsr & mode_mask) {
        case 0x0: /* EL0t: synchronous exception from a lower AArch64 EL. */
            vector_offset = 0x400;
            break;
        case 0x4: /* EL1t: synchronous exception at the current EL using SP0. */
            vector_offset = 0x000;
            break;
        case 0x5: /* EL1h: synchronous exception at the current EL using SPx. */
            vector_offset = 0x200;
            break;
        default:
            return false;
    }

    plan->target_spsr = (source_spsr & ~(mode_mask | daif_mask)) | 0x5 | daif_mask;
    plan->target_elr = vbar + vector_offset;
    return true;
}

#endif
