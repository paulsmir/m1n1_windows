/* SPDX-License-Identifier: MIT */

#include "hv.h"
#include "hv_agx_g2.generated.h"
#include "hv_agx_power_broker.h"
#include "utils.h"

static struct hv_agx_power_broker broker;
static DECLARE_SPINLOCK(broker_lock);
static bool broker_mapped;

static bool handle_agx_power_broker(struct exc_info *ctx, u64 addr, u64 *value, bool write,
                                    int width)
{
    bool handled;

    (void)ctx;
    if (addr < HV_AGX_G2_POWER_BROKER_BASE ||
        addr >= HV_AGX_G2_POWER_BROKER_BASE + HV_AGX_G2_POWER_BROKER_SIZE)
        return false;

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

bool hv_agx_power_broker_map(void)
{
    int ret;

    if (broker_mapped)
        return true;

    hv_agx_power_broker_init(&broker, hv_agx_power_j313_ops(), NULL);
    ret = hv_map_hook(HV_AGX_G2_POWER_BROKER_BASE, handle_agx_power_broker,
                      HV_AGX_G2_POWER_BROKER_SIZE);
    if (ret < 0) {
        printf("HV: AGX power broker map failed (%d)\n", ret);
        return false;
    }

    broker_mapped = true;
    printf("HV: AGX power broker mapped at 0x%lx..0x%lx (ABI %u)\n",
           (u64)HV_AGX_G2_POWER_BROKER_BASE,
           (u64)(HV_AGX_G2_POWER_BROKER_BASE + HV_AGX_G2_POWER_BROKER_SIZE),
           HV_AGX_POWER_ABI_VERSION);
    return true;
}
