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

static void test_finds_only_live_intids(void)
{
    const uint64_t pending = 1ULL << 62;
    const uint64_t active = 1ULL << 63;
    const uint64_t lrs[HV_VGIC_DIAG_LR_COUNT] = {
        pending | 17,
        active | 18,
        19, // An empty LR may retain INTID bits and must not count as a delivery.
        0,
    };

    assert(hv_vgic_diag_has_live_intid(lrs, 17));
    assert(hv_vgic_diag_has_live_intid(lrs, 18));
    assert(!hv_vgic_diag_has_live_intid(lrs, 19));
    assert(!hv_vgic_diag_has_live_intid(lrs, 20));
    assert(!hv_vgic_diag_has_live_intid(NULL, 17));
}

static void test_finds_the_live_lr_for_sgi_repending(void)
{
    const uint64_t pending = 1ULL << 62;
    const uint64_t active = 1ULL << 63;
    const uint64_t lrs[HV_VGIC_DIAG_LR_COUNT] = {
        5,                  // Empty LR retaining an old INTID.
        pending | 7,
        active | 5,
        pending | active | 9,
    };

    assert(hv_vgic_diag_find_live_intid(lrs, 5) == 2);
    assert(hv_vgic_diag_find_live_intid(lrs, 7) == 1);
    assert(hv_vgic_diag_find_live_intid(lrs, 9) == 3);
    assert(hv_vgic_diag_find_live_intid(lrs, 11) == -1);
    assert(hv_vgic_diag_find_live_intid(NULL, 5) == -1);
}

static void test_eoi_preserves_a_repending_interrupt(void)
{
    const uint64_t pending = 1ULL << 62;
    const uint64_t active = 1ULL << 63;
    const uint64_t payload = (0x20ULL << 48) | 7;

    assert(hv_vgic_diag_eoi_lr(active | payload) == 0);
    assert(hv_vgic_diag_eoi_lr(active | pending | payload) == (pending | payload));
}

static void test_timer_reexpiry_coalesces_into_the_active_lr(void)
{
    const uint64_t pending = 1ULL << 62;
    const uint64_t active = 1ULL << 63;
    const uint64_t timer = (0x20ULL << 48) | 18;
    uint64_t lrs[HV_VGIC_DIAG_LR_COUNT] = {
        active | timer,
        0,
    };

    assert(hv_vgic_diag_repend_live_intid(lrs, 18) == 0);
    assert(lrs[0] == (active | pending | timer));
    assert(lrs[1] == 0);
    assert(hv_vgic_diag_repend_live_intid(lrs, 19) == -1);
}

static void test_priority_is_masked_only_by_pmr_until_bpr_is_emulated(void)
{
    assert(hv_vgic_diag_priority_deliverable(0x20, 0xf8, 0xff));
    assert(hv_vgic_diag_priority_deliverable(0x10, 0xf8, 0x20));
    assert(hv_vgic_diag_priority_deliverable(0x20, 0xf8, 0x20));
    assert(hv_vgic_diag_priority_deliverable(0x40, 0xf8, 0x20));
    assert(!hv_vgic_diag_priority_deliverable(0x20, 0x20, 0xff));
}

int main(void)
{
    test_empty_lrs_are_not_occupied();
    test_pending_active_and_combined_states_are_counted();
    test_null_inputs_are_safe();
    test_finds_only_live_intids();
    test_finds_the_live_lr_for_sgi_repending();
    test_eoi_preserves_a_repending_interrupt();
    test_timer_reexpiry_coalesces_into_the_active_lr();
    test_priority_is_masked_only_by_pmr_until_bpr_is_emulated();
    puts("hv_vgic_diag_test: ok");
    return 0;
}
