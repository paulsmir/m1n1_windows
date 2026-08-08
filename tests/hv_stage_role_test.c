#include <assert.h>
#include <stdio.h>

#include "../src/hv_stage_role.h"

int main(void)
{
    assert(hv_stage_role_allows_bootstrap(HV_STAGE_ROLE_STAGE0));
    assert(!hv_stage_role_allows_autonomous(HV_STAGE_ROLE_STAGE0));
    assert(!hv_stage_role_allows_bootstrap(HV_STAGE_ROLE_STAGE1));
    assert(hv_stage_role_allows_autonomous(HV_STAGE_ROLE_STAGE1));
    assert(hv_stage_role_allows_bootstrap(HV_STAGE_ROLE_DEVELOPMENT));
    assert(hv_stage_role_allows_autonomous(HV_STAGE_ROLE_DEVELOPMENT));
    puts("hv_stage_role_test: ok");
    return 0;
}
