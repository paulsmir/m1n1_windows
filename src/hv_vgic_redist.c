/* SPDX-License-Identifier: MIT */

#include "hv_vgic_redist.h"

bool hv_vgic_redist_decode(u64 addr, u64 base, u32 num_cpus,
                           struct hv_vgic_redist_addr *out)
{
    if (!out || !num_cpus || addr < base)
        return false;

    u64 offset = addr - base;
    u64 cpu = offset / HV_VGIC_REDIST_STRIDE;
    if (cpu >= num_cpus)
        return false;

    out->cpu = cpu;
    out->reg = offset % HV_VGIC_REDIST_STRIDE;
    return true;
}
