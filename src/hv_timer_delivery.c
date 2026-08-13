/* SPDX-License-Identifier: MIT */

#include "hv_timer_delivery.h"

static int timer_slot(u32 intid)
{
    if (intid == 17)
        return 0;
    if (intid == 18)
        return 1;
    return -1;
}

bool hv_timer_delivery_assert(struct hv_timer_delivery_queue *queue, u32 intid, u8 priority)
{
    int slot = timer_slot(intid);
    if (!queue || slot < 0)
        return false;

    struct hv_timer_delivery_slot *entry = &queue->slots[slot];
    if (entry->asserted)
        return false;

    entry->delivery = (struct hv_timer_delivery){
        .intid = intid,
        .priority = priority,
    };
    entry->sequence = ++queue->next_sequence;
    entry->asserted = true;
    return true;
}

bool hv_timer_delivery_deassert(struct hv_timer_delivery_queue *queue, u32 intid)
{
    int slot = timer_slot(intid);
    if (!queue || slot < 0 || !queue->slots[slot].asserted)
        return false;

    queue->slots[slot].asserted = false;
    return true;
}

bool hv_timer_delivery_contains(const struct hv_timer_delivery_queue *queue, u32 intid)
{
    int slot = timer_slot(intid);
    return queue && slot >= 0 && queue->slots[slot].asserted;
}

bool hv_timer_delivery_pop(struct hv_timer_delivery_queue *queue,
                           struct hv_timer_delivery *out)
{
    if (!queue || !out)
        return false;

    int found = -1;
    for (u32 i = 0; i < HV_TIMER_DELIVERY_CAPACITY; i++) {
        if (!queue->slots[i].asserted)
            continue;
        if (found < 0 || queue->slots[i].sequence < queue->slots[found].sequence)
            found = i;
    }
    if (found < 0)
        return false;

    *out = queue->slots[found].delivery;
    queue->slots[found].asserted = false;
    return true;
}
