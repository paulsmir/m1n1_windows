#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/hv_diag.h"

struct exc_info {
    uint64_t elr;
    uint64_t spsr;
};

static unsigned collector_calls;

static void fake_collector(void *opaque, const struct exc_info *ctx,
                           struct hv_diag_sample_v1 *sample)
{
    const uint64_t bias = *(const uint64_t *)opaque;

    collector_calls++;
    sample->host_fiq_count = 100 + bias;
    sample->host_tick_count = 90 + bias;
    sample->guest_pc = ctx->elr;
    sample->guest_spsr = ctx->spsr;
    sample->nvme_sq_doorbells = 10 + bias;
    sample->nvme_cq_doorbells = 9 + bias;
    sample->nvme_commands = 8 + bias;
    sample->nvme_completions = 7 + bias;
    sample->fb_completed_frames = 6 + bias;
    sample->fb_backpressure_skips = 5 + bias;
    sample->vgic_pending_lrs = 4;
    sample->vgic_active_lrs = 3;
    sample->vgic_occupied_lrs = 2;
    sample->flags = HV_DIAG_FLAG_NVME_READY | HV_DIAG_FLAG_FB_ENABLED;
    sample->queues[0] = (struct hv_diag_queue_v1){1, 2, 3, 4};
    sample->queues[1] = (struct hv_diag_queue_v1){5, 6, 7, 8};
}

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

static void test_lifecycle_counters_are_independent_and_reset_together(void)
{
    uint64_t counters[HV_DIAG_COUNTER_COUNT];
    uint64_t before_invalid[HV_DIAG_COUNTER_COUNT];

    hv_diag_reset();
    for (unsigned counter = 0; counter < HV_DIAG_COUNTER_COUNT; counter++) {
        for (unsigned increment = 0; increment <= counter; increment++)
            hv_diag_count((enum hv_diag_counter)counter);
    }

    memset(counters, 0, sizeof(counters));
    hv_diag_get_counters(counters);
    for (unsigned counter = 0; counter < HV_DIAG_COUNTER_COUNT; counter++)
        assert(counters[counter] == counter + 1);

    memcpy(before_invalid, counters, sizeof(counters));
    hv_diag_count((enum hv_diag_counter)HV_DIAG_COUNTER_COUNT);
    hv_diag_count((enum hv_diag_counter)0xffffffffu);
    hv_diag_get_counters(counters);
    assert(memcmp(counters, before_invalid, sizeof(counters)) == 0);

    struct hv_diag_sample_v1 sample = sample_with_pc(0x4567);
    hv_diag_publish(&sample);
    hv_diag_reset();
    memset(counters, 0xa5, sizeof(counters));
    hv_diag_get_counters(counters);
    for (unsigned counter = 0; counter < HV_DIAG_COUNTER_COUNT; counter++)
        assert(counters[counter] == 0);
    struct hv_diag_status_v1 status;
    assert(hv_diag_get_status(&status));
    assert(status.count == 0);
}

static void test_irq_sources_and_lifecycle_stages_map_to_distinct_counters(void)
{
    uint64_t counters[HV_DIAG_COUNTER_COUNT];
    const uint32_t nvme_vintid = 64;

    hv_diag_reset();
    hv_diag_count_hw_irq(HV_DIAG_J313_XHCI_HW_IRQ);
    hv_diag_count_hw_irq(HV_DIAG_J313_XHCI_HW_IRQ + 1);

    hv_diag_count_vgic_irq(HV_DIAG_IRQ_INJECT, nvme_vintid, nvme_vintid);
    hv_diag_count_vgic_irq(HV_DIAG_IRQ_IAR, nvme_vintid, nvme_vintid);
    hv_diag_count_vgic_irq(HV_DIAG_IRQ_EOI, nvme_vintid, nvme_vintid);
    hv_diag_count_vgic_irq(HV_DIAG_IRQ_INJECT, HV_DIAG_J313_XHCI_VINTID, nvme_vintid);
    hv_diag_count_vgic_irq(HV_DIAG_IRQ_IAR, HV_DIAG_J313_XHCI_VINTID, nvme_vintid);
    hv_diag_count_vgic_irq(HV_DIAG_IRQ_EOI, HV_DIAG_J313_XHCI_VINTID, nvme_vintid);
    hv_diag_count_vgic_irq(HV_DIAG_IRQ_INJECT, 999, nvme_vintid);
    hv_diag_count_vgic_irq((enum hv_diag_irq_stage)99, nvme_vintid, nvme_vintid);

    hv_diag_get_counters(counters);
    for (unsigned counter = 0; counter < HV_DIAG_COUNTER_COUNT; counter++)
        assert(counters[counter] == 1);
}

static void test_tick_publishes_one_composed_sample_every_five_seconds(void)
{
    const uint64_t bias = 1;
    struct exc_info ctx = {.elr = 0xfffff80012345678ULL, .spsr = 0x60000085ULL};
    struct hv_diag_status_v1 status;
    struct hv_diag_sample_v1 sample;

    hv_diag_reset();
    collector_calls = 0;
    hv_diag_set_collector(fake_collector, (void *)&bias);
    hv_diag_count(HV_DIAG_NVME_IRQ_INJECT);
    hv_diag_count(HV_DIAG_NVME_IRQ_IAR);
    hv_diag_count(HV_DIAG_NVME_IRQ_EOI);
    hv_diag_count(HV_DIAG_XHCI_HW_IRQ);
    hv_diag_count(HV_DIAG_XHCI_IRQ_INJECT);
    hv_diag_count(HV_DIAG_XHCI_IRQ_IAR);
    hv_diag_count(HV_DIAG_XHCI_IRQ_EOI);

    for (unsigned tick = 1; tick < HV_DIAG_SAMPLE_TICKS; tick++)
        hv_diag_tick(&ctx);
    assert(hv_diag_get_status(&status));
    assert(status.count == 0);
    assert(collector_calls == 0);

    hv_diag_tick(&ctx);
    assert(hv_diag_get_status(&status));
    assert(status.count == 1);
    assert(collector_calls == 1);
    assert(hv_diag_get_sample(0, &sample));
    assert(sample.host_fiq_count == 101);
    assert(sample.host_tick_count == 91);
    assert(sample.guest_pc == ctx.elr);
    assert(sample.guest_spsr == ctx.spsr);
    assert(sample.nvme_sq_doorbells == 11);
    assert(sample.nvme_cq_doorbells == 10);
    assert(sample.nvme_commands == 9);
    assert(sample.nvme_completions == 8);
    assert(sample.nvme_irq_injects == 1);
    assert(sample.nvme_irq_iars == 1);
    assert(sample.nvme_irq_eois == 1);
    assert(sample.xhci_hw_irqs == 1);
    assert(sample.xhci_irq_injects == 1);
    assert(sample.xhci_irq_iars == 1);
    assert(sample.xhci_irq_eois == 1);
    assert(sample.fb_completed_frames == 7);
    assert(sample.fb_backpressure_skips == 6);
    assert(sample.vgic_pending_lrs == 4);
    assert(sample.vgic_active_lrs == 3);
    assert(sample.vgic_occupied_lrs == 2);
    assert(sample.flags == (HV_DIAG_FLAG_NVME_READY | HV_DIAG_FLAG_FB_ENABLED));
    assert(sample.queues[0].sq_head == 1 && sample.queues[0].cq_tail == 4);
    assert(sample.queues[1].sq_head == 5 && sample.queues[1].cq_tail == 8);

    ctx.elr += 4;
    for (unsigned tick = 0; tick < HV_DIAG_SAMPLE_TICKS; tick++)
        hv_diag_tick(&ctx);
    assert(hv_diag_get_status(&status));
    assert(status.count == 2);
    assert(collector_calls == 2);
    assert(hv_diag_get_sample(1, &sample));
    assert(sample.guest_pc == ctx.elr);
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
    test_lifecycle_counters_are_independent_and_reset_together();
    test_irq_sources_and_lifecycle_stages_map_to_distinct_counters();
    test_tick_publishes_one_composed_sample_every_five_seconds();
    puts("hv_diag_test: ok");
    return 0;
}
