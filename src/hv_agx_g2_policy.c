/* SPDX-License-Identifier: MIT */

#include "hv_agx_g2_policy.h"

static bool strings_equal(const char *left, const char *right)
{
    if (!left || !right)
        return false;

    while (*left && *right) {
        if (*left++ != *right++)
            return false;
    }

    return *left == *right;
}

bool hv_agx_g2_policy_validate(const struct hv_agx_g2_policy *policy)
{
    static const struct hv_agx_g2_interrupt_route expected_routes[] =
        HV_AGX_G2_INTERRUPT_ROUTE_VALUES;
    unsigned int index;

    if (!policy || !policy->routes)
        return false;
    if (!strings_equal(policy->profile_identity, HV_AGX_G2_PROFILE_IDENTITY))
        return false;
    if (!strings_equal(policy->source_contract_sha256, HV_AGX_G2_SOURCE_CONTRACT_SHA256))
        return false;
    if (policy->aperture_base != HV_AGX_G2_SGX_MMIO_BASE ||
        policy->aperture_size != HV_AGX_G2_SGX_MMIO_SIZE)
        return false;
    if (policy->route_count != HV_AGX_G2_INTERRUPT_ROUTE_COUNT)
        return false;
    if (policy->level != (bool)HV_AGX_G2_INTERRUPT_LEVEL ||
        policy->active_high != (bool)HV_AGX_G2_INTERRUPT_ACTIVE_HIGH ||
        policy->exclusive != (bool)HV_AGX_G2_INTERRUPT_EXCLUSIVE)
        return false;

    for (index = 0; index < HV_AGX_G2_INTERRUPT_ROUTE_COUNT; index++) {
        if (policy->routes[index].physical_intid != expected_routes[index].physical_intid ||
            policy->routes[index].guest_intid != expected_routes[index].guest_intid)
            return false;
    }

    return true;
}
