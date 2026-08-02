#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/hv_vgic_diag.h"

static void test_empty_lrs_are_not_occupied(void)
{
    const uint64_t lrs[HV_VGIC_DIAG_LR_COUNT] = {0};
    struct hv_vgic_diag_snapshot snapshot = {99, 99, 99};

    hv_vgic_diag_classify_lrs(lrs, &snapshot);
    assert(snapshot.pending_lrs == 0);
    assert(snapshot.active_lrs == 0);
    assert(snapshot.occupied_lrs == 0);
}

static void test_pending_active_and_combined_states_are_counted(void)
{
    const uint64_t pending = 1ULL << 62;
    const uint64_t active = 1ULL << 63;
    const uint64_t lrs[HV_VGIC_DIAG_LR_COUNT] = {
        pending,
        active,
        pending | active,
        0,
        0,
        0,
        0,
        0,
    };
    struct hv_vgic_diag_snapshot snapshot = {0};

    hv_vgic_diag_classify_lrs(lrs, &snapshot);
    assert(snapshot.pending_lrs == 2);
    assert(snapshot.active_lrs == 2);
    assert(snapshot.occupied_lrs == 3);
}

static void test_null_inputs_are_safe(void)
{
    const uint64_t lrs[HV_VGIC_DIAG_LR_COUNT] = {1ULL << 62};
    struct hv_vgic_diag_snapshot snapshot = {7, 8, 9};

    hv_vgic_diag_classify_lrs(NULL, &snapshot);
    assert(snapshot.pending_lrs == 0);
    assert(snapshot.active_lrs == 0);
    assert(snapshot.occupied_lrs == 0);
    hv_vgic_diag_classify_lrs(lrs, NULL);
}

int main(void)
{
    test_empty_lrs_are_not_occupied();
    test_pending_active_and_combined_states_are_counted();
    test_null_inputs_are_safe();
    puts("hv_vgic_diag_test: ok");
    return 0;
}
