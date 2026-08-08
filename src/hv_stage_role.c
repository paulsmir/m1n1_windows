/* SPDX-License-Identifier: MIT */

#include "hv_stage_role.h"

enum hv_stage_role hv_stage_role_current(void)
{
#if defined(M1N1_STAGE0)
    return HV_STAGE_ROLE_STAGE0;
#elif defined(M1N1_STAGE1)
    return HV_STAGE_ROLE_STAGE1;
#else
    return HV_STAGE_ROLE_DEVELOPMENT;
#endif
}

bool hv_stage_role_allows_bootstrap(enum hv_stage_role role)
{
    return role != HV_STAGE_ROLE_STAGE1;
}

bool hv_stage_role_allows_autonomous(enum hv_stage_role role)
{
    return role != HV_STAGE_ROLE_STAGE0;
}
