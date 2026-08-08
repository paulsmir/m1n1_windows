/* SPDX-License-Identifier: MIT */

#ifndef HV_STAGE_ROLE_H
#define HV_STAGE_ROLE_H

#include <stdbool.h>

enum hv_stage_role {
    HV_STAGE_ROLE_DEVELOPMENT,
    HV_STAGE_ROLE_STAGE0,
    HV_STAGE_ROLE_STAGE1,
};

enum hv_stage_role hv_stage_role_current(void);
bool hv_stage_role_allows_bootstrap(enum hv_stage_role role);
bool hv_stage_role_allows_autonomous(enum hv_stage_role role);

#endif
