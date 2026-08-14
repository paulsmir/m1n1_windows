/* SPDX-License-Identifier: MIT */

#ifndef HV_VGIC_DIAG_H
#define HV_VGIC_DIAG_H

#ifdef HV_VGIC_DIAG_HOST_TEST
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include "types.h"
#endif

#define HV_VGIC_DIAG_LR_COUNT 8u

struct hv_vgic_diag_snapshot {
    u32 pending_lrs;
    u32 active_lrs;
    u32 occupied_lrs;
};

struct hv_vgic_level_result {
    u64 lr;
    bool changed;
    bool newly_pending;
};

struct hv_vgic_timer_wake_transition {
    bool deliverable_latched;
    bool defer_wake;
};

void hv_vgic_diag_classify_lrs(const u64 lrs[HV_VGIC_DIAG_LR_COUNT],
                                struct hv_vgic_diag_snapshot *out);
int hv_vgic_diag_find_live_intid(const u64 lrs[HV_VGIC_DIAG_LR_COUNT], u32 intid);
bool hv_vgic_diag_has_live_intid(const u64 lrs[HV_VGIC_DIAG_LR_COUNT], u32 intid);
bool hv_vgic_diag_lr_needs_recovery_wake(u64 lr, u32 intid, u32 pmr,
                                         u32 running_priority);
int hv_vgic_diag_repend_live_intid(u64 lrs[HV_VGIC_DIAG_LR_COUNT], u32 intid);
struct hv_vgic_level_result hv_vgic_diag_sync_level_lr(u64 lr, bool asserted);
u64 hv_vgic_diag_eoi_lr(u64 lr);
bool hv_vgic_diag_priority_deliverable(u32 priority, u32 pmr, u32 running_priority);
struct hv_vgic_timer_wake_transition hv_vgic_diag_timer_wake_transition(
    bool deliverable_latched, bool signal, bool timer_signal);

#endif
