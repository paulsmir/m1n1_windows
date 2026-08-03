/* SPDX-License-Identifier: MIT */

#ifndef HV_SGI_DIAG_H
#define HV_SGI_DIAG_H

#ifdef HV_SGI_DIAG_HOST_TEST
#include <stdint.h>
typedef uint64_t u64;
#else
#include "types.h"
#endif

enum hv_sgi_diag_event {
    HV_SGI_DIAG_QUEUE,
    HV_SGI_DIAG_IPI_RX,
    HV_SGI_DIAG_DRAIN,
    HV_SGI_DIAG_INJECT,
    HV_SGI_DIAG_REPEND,
    HV_SGI_DIAG_NO_LR,
    HV_SGI_DIAG_IAR,
    HV_SGI_DIAG_EOI,
    HV_SGI_DIAG_EOI_ACTIVE_PENDING,
    HV_SGI_DIAG_EVENT_COUNT,
};

struct hv_sgi_diag_state {
    u64 counters[HV_SGI_DIAG_EVENT_COUNT];
};

struct hv_sgi_diag_snapshot {
    u64 queued;
    u64 ipi_received;
    u64 drained;
    u64 injected;
    u64 repended;
    u64 no_lr;
    u64 iars;
    u64 eois;
    u64 eoi_active_pending;
};

void hv_sgi_diag_note(struct hv_sgi_diag_state *state, enum hv_sgi_diag_event event);
void hv_sgi_diag_snapshot(const struct hv_sgi_diag_state *state,
                          struct hv_sgi_diag_snapshot *out);

#endif
