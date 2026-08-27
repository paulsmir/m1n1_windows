/* SPDX-License-Identifier: MIT */

#ifndef HV_AGX_G2_POLICY_H
#define HV_AGX_G2_POLICY_H

#ifdef HV_AGX_G2_HOST_TEST
#include <stdbool.h>
#include <stdint.h>
typedef uint64_t u64;
#else
#include "types.h"
#endif

#include "hv_agx_g2.generated.h"

struct hv_agx_g2_policy {
    const char *profile_identity;
    const char *source_contract_sha256;
    u64 aperture_base;
    u64 aperture_size;
    u64 gpu_region_base;
    u64 gpu_region_size;
    const struct hv_agx_g2_interrupt_route *routes;
    unsigned int route_count;
    bool level;
    bool active_high;
    bool exclusive;
};

bool hv_agx_g2_policy_validate(const struct hv_agx_g2_policy *policy);

#endif /* HV_AGX_G2_POLICY_H */
