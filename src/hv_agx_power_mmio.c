/* SPDX-License-Identifier: MIT */

#include "hv.h"
#include "adt.h"
#include "hv_agx_config_snapshot.h"
#include "hv_agx_g2.generated.h"
#include "hv_agx_power_broker.h"
#include "utils.h"

static struct hv_agx_power_broker broker;
static struct hv_agx_config_snapshot config_snapshot;
static bool config_snapshot_valid;
static DECLARE_SPINLOCK(broker_lock);
static bool resources_mapped;

static bool handle_agx_power_broker(struct exc_info *ctx, u64 addr, u64 *value, bool write,
                                    int width)
{
    bool handled;

    (void)ctx;
    if (addr < HV_AGX_G2_POWER_BROKER_BASE ||
        addr >= HV_AGX_G2_POWER_BROKER_BASE + HV_AGX_G2_POWER_BROKER_SIZE)
        return false;

    if (addr - HV_AGX_G2_POWER_BROKER_BASE >= HV_AGX_CONFIG_MMIO_OFFSET) {
        if (!config_snapshot_valid)
            return false;
        return hv_agx_config_snapshot_mmio(
            &config_snapshot,
            addr - HV_AGX_G2_POWER_BROKER_BASE - HV_AGX_CONFIG_MMIO_OFFSET,
            value, write, (unsigned)width);
    }

    spin_lock(&broker_lock);
    handled = hv_agx_power_broker_mmio(&broker, addr - HV_AGX_G2_POWER_BROKER_BASE, value,
                                       write, (unsigned)width);
    if (handled && write && addr - HV_AGX_G2_POWER_BROKER_BASE == HV_AGX_POWER_REG_COMMAND) {
        struct hv_agx_power_snapshot snapshot;

        hv_agx_power_broker_snapshot(&broker, &snapshot);
        printf("HV: AGX power receipt seq=%lu cmd=%lu state=%u result=%u\n",
               snapshot.receipt_sequence, *value, snapshot.state, snapshot.result);
    }
    spin_unlock(&broker_lock);
    return handled;
}

bool hv_agx_g2_resources_map(void)
{
    int ret;

    if (resources_mapped)
        return true;

    ret = hv_map_sw(HV_AGX_G2_GPU_BASE, HV_AGX_G2_GPU_BASE,
                    HV_AGX_G2_GPU_SIZE);
    if (ret < 0) {
        printf("HV: AGX gpu-region stage-2 map failed (%d)\n", ret);
        return false;
    }

    hv_agx_power_broker_init(&broker, hv_agx_power_j313_ops(), NULL);
    config_snapshot_valid = hv_agx_config_snapshot_from_adt(adt, &config_snapshot);
    if (!config_snapshot_valid)
        printf("HV: AGX boot config snapshot unavailable; firmware start must fail closed\n");
    ret = hv_map_hook(HV_AGX_G2_POWER_BROKER_BASE, handle_agx_power_broker,
                      HV_AGX_G2_POWER_BROKER_SIZE);
    if (ret < 0) {
        printf("HV: AGX power broker map failed (%d)\n", ret);
        return false;
    }

    resources_mapped = true;
    printf("HV: AGX gpu-region mapped at 0x%lx..0x%lx\n",
           (u64)HV_AGX_G2_GPU_BASE,
           (u64)(HV_AGX_G2_GPU_BASE + HV_AGX_G2_GPU_SIZE));
    printf("HV: AGX power broker mapped at 0x%lx..0x%lx (ABI %u)\n",
           (u64)HV_AGX_G2_POWER_BROKER_BASE,
           (u64)(HV_AGX_G2_POWER_BROKER_BASE + HV_AGX_G2_POWER_BROKER_SIZE),
           HV_AGX_POWER_ABI_VERSION);
    if (config_snapshot_valid)
        printf("HV: AGX boot config snapshot v%u at broker+0x%x (%u pstates, %u scalars, mask=0x%llx)\n",
               HV_AGX_CONFIG_ABI_VERSION, HV_AGX_CONFIG_MMIO_OFFSET,
               config_snapshot.perf_state_count,
               (unsigned)__builtin_popcountll(config_snapshot.scalar_presence),
               (unsigned long long)config_snapshot.scalar_presence);
    return true;
}
