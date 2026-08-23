#include <assert.h>
#include "../src/hv_wfx_policy.h"

int main(void)
{
    assert(!hv_wfx_is_wfe(0));
    assert(hv_wfx_is_wfe(HV_WFX_ISS_IS_WFE));
    assert(hv_wfx_resume_pc(0x1000) == 0x1004);
    assert(hv_wfx_hcr_mask() == (1ULL << 13));
    assert(hv_wfx_apply_hcr(0x123) == (0x123 | (1ULL << 13)));
    assert(hv_wfx_policy_satisfied(hv_wfx_hcr_mask()));
    assert(!hv_wfx_policy_satisfied(0));
    assert(hv_wfx_policy_satisfied(1ULL << 13));
    assert(!hv_wfx_policy_satisfied(1ULL << 14));

    assert(hv_wfx_trap_action(0, false) == HV_WFX_WAIT_WFI);
    assert(hv_wfx_trap_action(HV_WFX_ISS_IS_WFE, false) == HV_WFX_RESUME_GUEST);
    assert(hv_wfx_trap_action(0, true) == HV_WFX_RESUME_GUEST);
    assert(hv_wfx_trap_action(HV_WFX_ISS_IS_WFE, true) == HV_WFX_RESUME_GUEST);
    return 0;
}
