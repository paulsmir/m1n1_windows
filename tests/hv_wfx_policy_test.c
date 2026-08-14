#include <assert.h>
#include "../src/hv_wfx_policy.h"

int main(void)
{
    assert(!hv_wfx_is_wfe(0));
    assert(hv_wfx_is_wfe(HV_WFX_ISS_IS_WFE));
    assert(hv_wfx_resume_pc(0x1000) == 0x1004);
    assert(hv_wfx_hcr_mask() == ((1ULL << 13) | (1ULL << 14)));
    assert(hv_wfx_apply_pending_hcr(0x123, true) ==
           (0x123 | (1ULL << 13) | (1ULL << 14)));
    assert(hv_wfx_apply_pending_hcr(~0ULL, false) ==
           (~0ULL & ~((1ULL << 13) | (1ULL << 14))));
    assert(hv_wfx_pending_hcr_satisfied(hv_wfx_hcr_mask(), true));
    assert(hv_wfx_pending_hcr_satisfied(0, false));
    assert(!hv_wfx_pending_hcr_satisfied(1ULL << 13, true));
    assert(!hv_wfx_pending_hcr_satisfied(1ULL << 14, false));
    return 0;
}
