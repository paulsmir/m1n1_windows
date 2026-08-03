#include <assert.h>
#include <stdio.h>

#include "../src/hv_sgi_diag.h"

static void test_each_stage_is_counted_independently(void)
{
    struct hv_sgi_diag_state state = {0};
    struct hv_sgi_diag_snapshot snapshot = {0};

    hv_sgi_diag_note(&state, HV_SGI_DIAG_QUEUE);
    hv_sgi_diag_note(&state, HV_SGI_DIAG_IPI_RX);
    hv_sgi_diag_note(&state, HV_SGI_DIAG_DRAIN);
    hv_sgi_diag_note(&state, HV_SGI_DIAG_INJECT);
    hv_sgi_diag_note(&state, HV_SGI_DIAG_REPEND);
    hv_sgi_diag_note(&state, HV_SGI_DIAG_NO_LR);
    hv_sgi_diag_note(&state, HV_SGI_DIAG_IAR);
    hv_sgi_diag_note(&state, HV_SGI_DIAG_EOI);
    hv_sgi_diag_note(&state, HV_SGI_DIAG_EOI_ACTIVE_PENDING);
    hv_sgi_diag_snapshot(&state, &snapshot);

    assert(snapshot.queued == 1);
    assert(snapshot.ipi_received == 1);
    assert(snapshot.drained == 1);
    assert(snapshot.injected == 1);
    assert(snapshot.repended == 1);
    assert(snapshot.no_lr == 1);
    assert(snapshot.iars == 1);
    assert(snapshot.eois == 1);
    assert(snapshot.eoi_active_pending == 1);
}

static void test_repeated_events_accumulate(void)
{
    struct hv_sgi_diag_state state = {0};
    struct hv_sgi_diag_snapshot snapshot = {0};

    hv_sgi_diag_note(&state, HV_SGI_DIAG_QUEUE);
    hv_sgi_diag_note(&state, HV_SGI_DIAG_QUEUE);
    hv_sgi_diag_note(&state, HV_SGI_DIAG_EOI);
    hv_sgi_diag_snapshot(&state, &snapshot);

    assert(snapshot.queued == 2);
    assert(snapshot.eois == 1);
}

static void test_invalid_inputs_are_safe(void)
{
    struct hv_sgi_diag_state state = {0};
    struct hv_sgi_diag_snapshot snapshot = {0};

    hv_sgi_diag_note(NULL, HV_SGI_DIAG_QUEUE);
    hv_sgi_diag_note(&state, HV_SGI_DIAG_EVENT_COUNT);
    hv_sgi_diag_snapshot(NULL, &snapshot);
    hv_sgi_diag_snapshot(&state, NULL);
    hv_sgi_diag_snapshot(&state, &snapshot);
    assert(snapshot.queued == 0);
    assert(snapshot.eois == 0);
}

int main(void)
{
    test_each_stage_is_counted_independently();
    test_repeated_events_accumulate();
    test_invalid_inputs_are_safe();
    puts("hv_sgi_diag_test: ok");
    return 0;
}
