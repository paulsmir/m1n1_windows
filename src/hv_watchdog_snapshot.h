/* SPDX-License-Identifier: MIT */

#ifndef HV_WATCHDOG_SNAPSHOT_H
#define HV_WATCHDOG_SNAPSHOT_H

#ifdef HV_WATCHDOG_SNAPSHOT_HOST_TEST
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include "types.h"
#endif

#define HV_WATCHDOG_SNAPSHOT_LR_COUNT 8

struct hv_watchdog_cpu_sample {
    u64 cpu;
    u64 pc;
    u64 spsr;
    u64 cntpct;
    u64 cntvct;
    u64 cntvoff;
    u64 cntp_ctl;
    u64 cntp_cval;
    u64 cntv_ctl;
    u64 cntv_cval;
    u64 vm_tmr_fiq_ena;
    u64 hcr;
    u64 ich_hcr;
    u64 ich_vmcr;
    u64 isr;
    u64 timer_p_injected;
    u64 timer_v_injected;
    u64 timer_queue_depth;
    u64 irq_queue_depth;
    u64 sgi_pending_mask;
    u64 sgi_queued;
    u64 sgi_ipi_received;
    u64 sgi_drained;
    u64 sgi_injected;
    u64 sgi_repended;
    u64 sgi_no_lr;
    u64 sgi_iar;
    u64 sgi_eoi;
    u64 sgi_eoi_active_pending;
    u64 last_sgi_from;
    u64 last_sgi_intid;
    u64 last_iar_intid;
    u64 last_eoi_intid;
    u64 last_iar_tick;
    u64 last_eoi_tick;
    u64 last_el2_marker;
    u64 lr_count;
    u64 lrs[HV_WATCHDOG_SNAPSHOT_LR_COUNT];
};

struct hv_watchdog_cpu_record {
    u32 sequence;
    struct hv_watchdog_cpu_sample sample;
};

void hv_watchdog_snapshot_publish(struct hv_watchdog_cpu_record *record,
                                  const struct hv_watchdog_cpu_sample *sample);
bool hv_watchdog_snapshot_read(const struct hv_watchdog_cpu_record *record,
                               struct hv_watchdog_cpu_sample *sample);
bool hv_watchdog_snapshot_due(u64 per_cpu_sample_tick);
bool hv_watchdog_snapshot_dump_due(u64 current_tick, u64 previous_tick, u64 interval);

#endif
