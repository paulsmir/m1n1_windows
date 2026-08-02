/* SPDX-License-Identifier: MIT */

#include "hv_diag.h"

#ifdef HV_DIAG_HOST_TEST
#include <string.h>
#else
#include "string.h"
#endif

static struct {
    struct hv_diag_sample_v1 samples[HV_DIAG_RING_CAPACITY];
    u64 counters[HV_DIAG_COUNTER_COUNT];
    u64 next_sequence;
    u32 count;
    u32 ticks;
} ring;

static hv_diag_collect_fn collector;
static void *collector_opaque;

void hv_diag_reset(void)
{
    memset(&ring, 0, sizeof(ring));
}

void hv_diag_publish(const struct hv_diag_sample_v1 *sample)
{
    if (!sample)
        return;

    struct hv_diag_sample_v1 published = *sample;
    u64 sequence = ring.next_sequence;

    published.sequence = sequence;
    ring.samples[sequence & (HV_DIAG_RING_CAPACITY - 1)] = published;
    ring.next_sequence = sequence + 1;
    if (ring.count < HV_DIAG_RING_CAPACITY)
        ring.count++;
}

bool hv_diag_get_status(struct hv_diag_status_v1 *out)
{
    if (!out)
        return false;

    *out = (struct hv_diag_status_v1){
        .abi_version = HV_DIAG_ABI_VERSION,
        .sample_size = sizeof(struct hv_diag_sample_v1),
        .capacity = HV_DIAG_RING_CAPACITY,
        .count = ring.count,
        .oldest_sequence = ring.next_sequence - ring.count,
        .next_sequence = ring.next_sequence,
    };
    return true;
}

bool hv_diag_get_sample(u64 sequence, struct hv_diag_sample_v1 *out)
{
    u64 oldest_sequence = ring.next_sequence - ring.count;

    if (!out || sequence < oldest_sequence || sequence >= ring.next_sequence)
        return false;

    const struct hv_diag_sample_v1 *sample =
        &ring.samples[sequence & (HV_DIAG_RING_CAPACITY - 1)];
    if (sample->sequence != sequence)
        return false;

    *out = *sample;
    return true;
}

void hv_diag_count(enum hv_diag_counter counter)
{
    u32 index = (u32)counter;

    if (index >= HV_DIAG_COUNTER_COUNT)
        return;
    __atomic_add_fetch(&ring.counters[index], 1, __ATOMIC_RELAXED);
}

void hv_diag_get_counters(u64 out[HV_DIAG_COUNTER_COUNT])
{
    if (!out)
        return;

    for (u32 i = 0; i < HV_DIAG_COUNTER_COUNT; i++)
        out[i] = __atomic_load_n(&ring.counters[i], __ATOMIC_RELAXED);
}

void hv_diag_count_hw_irq(u32 hw_irq)
{
    if (hw_irq == HV_DIAG_J313_XHCI_HW_IRQ)
        hv_diag_count(HV_DIAG_XHCI_HW_IRQ);
}

void hv_diag_count_vgic_irq(enum hv_diag_irq_stage stage, u32 vintid, u32 nvme_vintid)
{
    u32 stage_index = (u32)stage;

    if (stage_index > HV_DIAG_IRQ_EOI)
        return;
    if (vintid == nvme_vintid)
        hv_diag_count((enum hv_diag_counter)(HV_DIAG_NVME_IRQ_INJECT + stage_index));
    else if (vintid == HV_DIAG_J313_XHCI_VINTID)
        hv_diag_count((enum hv_diag_counter)(HV_DIAG_XHCI_IRQ_INJECT + stage_index));
}

void hv_diag_set_collector(hv_diag_collect_fn collect, void *opaque)
{
    collector = collect;
    collector_opaque = opaque;
}

void hv_diag_tick(const struct exc_info *ctx)
{
    if (++ring.ticks < HV_DIAG_SAMPLE_TICKS)
        return;
    ring.ticks = 0;
    if (!collector)
        return;

    struct hv_diag_sample_v1 sample = {0};
    u64 counters[HV_DIAG_COUNTER_COUNT];

    collector(collector_opaque, ctx, &sample);
    hv_diag_get_counters(counters);
    sample.nvme_irq_injects = counters[HV_DIAG_NVME_IRQ_INJECT];
    sample.nvme_irq_iars = counters[HV_DIAG_NVME_IRQ_IAR];
    sample.nvme_irq_eois = counters[HV_DIAG_NVME_IRQ_EOI];
    sample.xhci_hw_irqs = counters[HV_DIAG_XHCI_HW_IRQ];
    sample.xhci_irq_injects = counters[HV_DIAG_XHCI_IRQ_INJECT];
    sample.xhci_irq_iars = counters[HV_DIAG_XHCI_IRQ_IAR];
    sample.xhci_irq_eois = counters[HV_DIAG_XHCI_IRQ_EOI];
    hv_diag_publish(&sample);
}
