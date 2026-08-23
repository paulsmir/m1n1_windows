/* SPDX-License-Identifier: MIT */

#include "hv_vgic_diag.h"

void hv_vgic_diag_classify_lrs(const u64 lrs[HV_VGIC_DIAG_LR_COUNT],
                                struct hv_vgic_diag_snapshot *out)
{
    if (!out)
        return;

    *out = (struct hv_vgic_diag_snapshot){0};
    if (!lrs)
        return;

    for (u32 i = 0; i < HV_VGIC_DIAG_LR_COUNT; i++) {
        u32 state = (lrs[i] >> 62) & 3;
        if (state & 1)
            out->pending_lrs++;
        if (state & 2)
            out->active_lrs++;
        if (state)
            out->occupied_lrs++;
    }
}

int hv_vgic_diag_find_live_intid(const u64 lrs[HV_VGIC_DIAG_LR_COUNT], u32 intid)
{
    if (!lrs)
        return -1;

    for (u32 i = 0; i < HV_VGIC_DIAG_LR_COUNT; i++) {
        u64 state = (lrs[i] >> 62) & 3;
        u32 lr_intid = lrs[i] & 0xffffffff;
        if (state && lr_intid == intid)
            return i;
    }
    return -1;
}

bool hv_vgic_diag_has_live_intid(const u64 lrs[HV_VGIC_DIAG_LR_COUNT], u32 intid)
{
    return hv_vgic_diag_find_live_intid(lrs, intid) >= 0;
}

bool hv_vgic_diag_lr_needs_recovery_wake(u64 lr, u32 intid, u32 pmr,
                                         u32 running_priority)
{
    const u32 state = (lr >> 62) & 3;
    const u32 lr_intid = lr & 0xffffffff;
    const u32 priority = (lr >> 48) & 0xff;

    /*
     * A physical recovery wake is useful only before the guest has acknowledged
     * the timer.  Active and Active+Pending mean Windows is already inside the
     * interrupt lifecycle; polling EL2 at 1 kHz there adds scheduler latency but
     * cannot make that handler finish sooner.
     */
    return state == 1 && lr_intid == intid &&
           hv_vgic_diag_priority_deliverable(priority, pmr, running_priority);
}

int hv_vgic_diag_repend_live_intid(u64 lrs[HV_VGIC_DIAG_LR_COUNT], u32 intid)
{
    int lr = hv_vgic_diag_find_live_intid(lrs, intid);
    if (lr >= 0)
        lrs[lr] |= 1ULL << 62;
    return lr;
}

struct hv_vgic_level_result hv_vgic_diag_sync_level_lr(u64 lr, bool asserted)
{
    const u64 pending = 1ULL << 62;
    u64 next = asserted ? lr | pending : lr & ~pending;

    return (struct hv_vgic_level_result){
        .lr = next,
        .changed = next != lr,
        .newly_pending = asserted && !(lr & pending),
    };
}

u64 hv_vgic_diag_eoi_lr(u64 lr)
{
    const u64 pending = 1ULL << 62;
    const u64 active = 1ULL << 63;

    if (lr & pending)
        return lr & ~active;
    return 0;
}

bool hv_vgic_diag_priority_deliverable(u32 priority, u32 pmr, u32 running_priority)
{
    /*
     * Until ICC_BPR1_EL1 and group-priority preemption are modelled, comparing raw
     * priorities against the active LR can deadlock the guest: Windows may leave an SGI
     * active while a timer of the same group priority must still be observed.  PMR is the
     * invariant we do emulate correctly.  Keep running_priority in the API so telemetry
     * and the eventual BPR-aware implementation share the same call sites.
     */
    (void)running_priority;
    return priority < pmr;
}
