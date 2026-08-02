#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/hv_diag.h"

static struct hv_diag_sample_v1 sample_with_pc(uint64_t pc)
{
    struct hv_diag_sample_v1 sample = {0};

    sample.sequence = 0xfeedfaceULL;
    sample.guest_pc = pc;
    sample.guest_spsr = 0x60000085ULL;
    sample.nvme_commands = pc + 10;
    sample.queues[0].sq_head = (uint16_t)pc;
    sample.queues[1].cq_tail = (uint16_t)(pc + 1);
    return sample;
}

static void test_reset_reports_empty_versioned_ring(void)
{
    struct hv_diag_status_v1 status = {0};

    hv_diag_reset();
    assert(hv_diag_get_status(&status));
    assert(status.abi_version == 1);
    assert(status.sample_size == 176);
    assert(status.capacity == 256);
    assert(status.count == 0);
    assert(status.oldest_sequence == 0);
    assert(status.next_sequence == 0);
}

static void test_publish_assigns_sequences_and_returns_value_copies(void)
{
    struct hv_diag_sample_v1 published;

    hv_diag_reset();
    for (uint64_t pc = 10; pc < 13; pc++) {
        struct hv_diag_sample_v1 sample = sample_with_pc(pc);
        hv_diag_publish(&sample);
        assert(sample.sequence == 0xfeedfaceULL);
    }

    for (uint64_t sequence = 0; sequence < 3; sequence++) {
        memset(&published, 0, sizeof(published));
        assert(hv_diag_get_sample(sequence, &published));
        assert(published.sequence == sequence);
        assert(published.guest_pc == sequence + 10);
        assert(published.nvme_commands == sequence + 20);
    }

    published.guest_pc = 0;
    assert(hv_diag_get_sample(1, &published));
    assert(published.guest_pc == 11);
}

static void test_wrap_retains_only_newest_capacity(void)
{
    struct hv_diag_status_v1 status;
    struct hv_diag_sample_v1 sample;

    hv_diag_reset();
    for (uint64_t sequence = 0; sequence < HV_DIAG_RING_CAPACITY + 3; sequence++) {
        sample = sample_with_pc(0x1000 + sequence);
        hv_diag_publish(&sample);
    }

    assert(hv_diag_get_status(&status));
    assert(status.count == 256);
    assert(status.oldest_sequence == 3);
    assert(status.next_sequence == 259);
    assert(!hv_diag_get_sample(0, &sample));
    assert(!hv_diag_get_sample(1, &sample));
    assert(!hv_diag_get_sample(2, &sample));

    for (uint64_t sequence = 3; sequence < 259; sequence++) {
        assert(hv_diag_get_sample(sequence, &sample));
        assert(sample.sequence == sequence);
        assert(sample.guest_pc == 0x1000 + sequence);
    }
}

static void test_invalid_queries_do_not_modify_state_or_output(void)
{
    struct hv_diag_status_v1 before;
    struct hv_diag_status_v1 after;
    struct hv_diag_sample_v1 output;
    struct hv_diag_sample_v1 sentinel;

    hv_diag_reset();
    struct hv_diag_sample_v1 sample = sample_with_pc(0x1234);
    hv_diag_publish(&sample);
    assert(hv_diag_get_status(&before));

    memset(&sentinel, 0xa5, sizeof(sentinel));
    output = sentinel;
    assert(!hv_diag_get_status(NULL));
    assert(!hv_diag_get_sample(0, NULL));
    assert(!hv_diag_get_sample(before.next_sequence, &output));
    assert(memcmp(&output, &sentinel, sizeof(output)) == 0);

    assert(hv_diag_get_status(&after));
    assert(memcmp(&before, &after, sizeof(before)) == 0);
}

int main(void)
{
    _Static_assert((HV_DIAG_RING_CAPACITY & (HV_DIAG_RING_CAPACITY - 1)) == 0,
                   "diagnostic ring capacity must be a power of two");
    _Static_assert(sizeof(struct hv_diag_sample_v1) == 176, "diagnostic sample ABI size");
    _Static_assert(sizeof(struct hv_diag_status_v1) == 32, "diagnostic status ABI size");

    test_reset_reports_empty_versioned_ring();
    test_publish_assigns_sequences_and_returns_value_copies();
    test_wrap_retains_only_newest_capacity();
    test_invalid_queries_do_not_modify_state_or_output();
    puts("hv_diag_test: ok");
    return 0;
}
