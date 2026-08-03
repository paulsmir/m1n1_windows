/* SPDX-License-Identifier: MIT */

#include "hv_sgi_diag.h"

void hv_sgi_diag_note(struct hv_sgi_diag_state *state, enum hv_sgi_diag_event event)
{
    if (!state || event < 0 || event >= HV_SGI_DIAG_EVENT_COUNT)
        return;

    __atomic_add_fetch(&state->counters[event], 1, __ATOMIC_RELAXED);
}

void hv_sgi_diag_snapshot(const struct hv_sgi_diag_state *state,
                          struct hv_sgi_diag_snapshot *out)
{
    if (!state || !out)
        return;

    *out = (struct hv_sgi_diag_snapshot){
        .queued = __atomic_load_n(&state->counters[HV_SGI_DIAG_QUEUE], __ATOMIC_RELAXED),
        .ipi_received =
            __atomic_load_n(&state->counters[HV_SGI_DIAG_IPI_RX], __ATOMIC_RELAXED),
        .drained = __atomic_load_n(&state->counters[HV_SGI_DIAG_DRAIN], __ATOMIC_RELAXED),
        .injected = __atomic_load_n(&state->counters[HV_SGI_DIAG_INJECT], __ATOMIC_RELAXED),
        .repended = __atomic_load_n(&state->counters[HV_SGI_DIAG_REPEND], __ATOMIC_RELAXED),
        .no_lr = __atomic_load_n(&state->counters[HV_SGI_DIAG_NO_LR], __ATOMIC_RELAXED),
        .iars = __atomic_load_n(&state->counters[HV_SGI_DIAG_IAR], __ATOMIC_RELAXED),
        .eois = __atomic_load_n(&state->counters[HV_SGI_DIAG_EOI], __ATOMIC_RELAXED),
        .eoi_active_pending = __atomic_load_n(
            &state->counters[HV_SGI_DIAG_EOI_ACTIVE_PENDING], __ATOMIC_RELAXED),
    };
}
