/* SPDX-License-Identifier: MIT */

#include "hv_agx_power_broker.h"

#ifdef HV_AGX_POWER_BROKER_HOST_TEST
int pmgr_adt_power_enable(const char *path);
int pmgr_adt_power_disable(const char *path);
#else
#include "pmgr.h"
#endif

/*
 * Keep the platform side deliberately narrower than the guest ABI: Windows can
 * request a transition, but it can never select an arbitrary ADT/PMGR node.
 */
static bool j313_asc_on(void *opaque)
{
    (void)opaque;
    return pmgr_adt_power_enable("/arm-io/gfx-asc") == 0;
}

static bool j313_sgx_on(void *opaque)
{
    (void)opaque;
    return pmgr_adt_power_enable("/arm-io/sgx") == 0;
}

static bool j313_sgx_off(void *opaque)
{
    (void)opaque;
    return pmgr_adt_power_disable("/arm-io/sgx") == 0;
}

static bool j313_asc_off(void *opaque)
{
    (void)opaque;
    return pmgr_adt_power_disable("/arm-io/gfx-asc") == 0;
}

const struct hv_agx_power_ops *hv_agx_power_j313_ops(void)
{
    static const struct hv_agx_power_ops ops = {
        .asc_on = j313_asc_on,
        .sgx_on = j313_sgx_on,
        .sgx_off = j313_sgx_off,
        .asc_off = j313_asc_off,
    };

    return &ops;
}
