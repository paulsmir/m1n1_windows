/* SPDX-License-Identifier: MIT */

#include "hv_diag.h"

#ifdef HV_DIAG_HOST_TEST
#include <string.h>
#else
#include "string.h"
#endif

static struct {
    struct hv_diag_sample_v1 samples[HV_DIAG_RING_CAPACITY];
    u64 next_sequence;
    u32 count;
} ring;

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
