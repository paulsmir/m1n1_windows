/* SPDX-License-Identifier: MIT */
#ifndef HV_WFX_POLICY_H
#define HV_WFX_POLICY_H

#ifdef HV_WFX_POLICY_HOST_TEST
#include <stdbool.h>
#include <stdint.h>
typedef uint64_t u64;
#define HV_WFX_BIT(n) (1ULL << (n))
#else
#include "types.h"
#define HV_WFX_BIT(n) BIT(n)
#endif

/* ESR_EL2.ISS.TI is one for WFE and zero for WFI. */
#define HV_WFX_ISS_IS_WFE HV_WFX_BIT(0)

static inline bool hv_wfx_is_wfe(u64 iss)
{
    return !!(iss & HV_WFX_ISS_IS_WFE);
}

static inline u64 hv_wfx_resume_pc(u64 trapped_pc)
{
    return trapped_pc + 4;
}

static inline u64 hv_wfx_hcr_mask(void)
{
    return HV_WFX_BIT(13) | HV_WFX_BIT(14);
}

static inline u64 hv_wfx_apply_pending_hcr(u64 hcr, bool virtual_irq_pending)
{
    if (virtual_irq_pending)
        return hcr | hv_wfx_hcr_mask();
    return hcr & ~hv_wfx_hcr_mask();
}

static inline bool hv_wfx_pending_hcr_satisfied(u64 hcr, bool virtual_irq_pending)
{
    return (hcr & hv_wfx_hcr_mask()) ==
           (virtual_irq_pending ? hv_wfx_hcr_mask() : 0);
}

#endif
