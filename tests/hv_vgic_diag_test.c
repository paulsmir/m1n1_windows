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

static void test_recovery_wake_requires_pending_only_timer_lr(void)
{
    const uint64_t pending = 1ULL << 62;
    const uint64_t active = 1ULL << 63;
    const uint64_t timer = (0x20ULL << 48) | 18;

    assert(hv_vgic_diag_lr_needs_recovery_wake(pending | timer, 18, 0xf8, 0xff));
    assert(!hv_vgic_diag_lr_needs_recovery_wake(active | timer, 18, 0xf8, 0xff));
    assert(!hv_vgic_diag_lr_needs_recovery_wake(active | pending | timer, 18,
                                                0xf8, 0xff));
    assert(!hv_vgic_diag_lr_needs_recovery_wake(pending | timer, 17, 0xf8, 0xff));
    assert(!hv_vgic_diag_lr_needs_recovery_wake(pending | timer, 18, 0x20, 0xff));
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

static void test_level_sync_covers_every_lr_state(void)
{
    const uint64_t pending = 1ULL << 62;
    const uint64_t active = 1ULL << 63;
    const uint64_t payload = (0x20ULL << 48) | 18;
    const struct {
        uint64_t before;
        bool asserted;
        uint64_t after;
        bool changed;
        bool newly_pending;
    } cases[] = {
        {payload, false, payload, false, false},
        {pending | payload, false, payload, true, false},
        {active | payload, false, active | payload, false, false},
        {active | pending | payload, false, active | payload, true, false},
        {pending | payload, true, pending | payload, false, false},
        {active | payload, true, active | pending | payload, true, true},
        {active | pending | payload, true, active | pending | payload, false, false},
    };

    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        struct hv_vgic_level_result got =
            hv_vgic_diag_sync_level_lr(cases[i].before, cases[i].asserted);
        assert(got.lr == cases[i].after);
        assert(got.changed == cases[i].changed);
        assert(got.newly_pending == cases[i].newly_pending);
    }
}

static void test_priority_is_masked_only_by_pmr_until_bpr_is_emulated(void)
{
    assert(hv_vgic_diag_priority_deliverable(0x20, 0xf8, 0xff));
    assert(hv_vgic_diag_priority_deliverable(0x10, 0xf8, 0x20));
    assert(hv_vgic_diag_priority_deliverable(0x20, 0xf8, 0x20));
    assert(hv_vgic_diag_priority_deliverable(0x40, 0xf8, 0x20));
    assert(!hv_vgic_diag_priority_deliverable(0x20, 0x20, 0xff));
}

static void test_physical_timer_wake_occurs_once_per_deliverable_pending_interval(void)
{
    struct hv_vgic_timer_wake_transition next =
        hv_vgic_diag_timer_wake_transition(false, true, true);
    assert(next.deliverable_latched);
    assert(next.defer_wake);

    next = hv_vgic_diag_timer_wake_transition(true, true, true);
    assert(next.deliverable_latched);
    assert(!next.defer_wake);

    next = hv_vgic_diag_timer_wake_transition(true, false, true);
    assert(!next.deliverable_latched);
    assert(!next.defer_wake);

    next = hv_vgic_diag_timer_wake_transition(false, true, false);
    assert(!next.deliverable_latched);
    assert(!next.defer_wake);

    next = hv_vgic_diag_timer_wake_transition(false, true, true);
    assert(next.deliverable_latched);
    assert(next.defer_wake);
}

int main(void)
{
    test_empty_lrs_are_not_occupied();
    test_pending_active_and_combined_states_are_counted();
    test_null_inputs_are_safe();
    test_finds_only_live_intids();
    test_recovery_wake_requires_pending_only_timer_lr();
    test_finds_the_live_lr_for_sgi_repending();
    test_eoi_preserves_a_repending_interrupt();
    test_timer_reexpiry_coalesces_into_the_active_lr();
    test_level_sync_covers_every_lr_state();
    test_priority_is_masked_only_by_pmr_until_bpr_is_emulated();
    test_physical_timer_wake_occurs_once_per_deliverable_pending_interval();
    puts("hv_vgic_diag_test: ok");
    return 0;
}
