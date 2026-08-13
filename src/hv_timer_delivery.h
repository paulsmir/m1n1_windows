/* SPDX-License-Identifier: MIT */

#ifndef HV_TIMER_DELIVERY_H
#define HV_TIMER_DELIVERY_H

#ifdef HV_TIMER_DELIVERY_HOST_TEST
#include <stdbool.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include "types.h"
#endif

#define HV_TIMER_DELIVERY_CAPACITY 2u

struct hv_timer_delivery {
    u32 intid;
    u8 priority;
};

struct hv_timer_delivery_slot {
    struct hv_timer_delivery delivery;
    u64 sequence;
    bool asserted;
};

struct hv_timer_delivery_queue {
    struct hv_timer_delivery_slot slots[HV_TIMER_DELIVERY_CAPACITY];
    u64 next_sequence;
};

bool hv_timer_delivery_assert(struct hv_timer_delivery_queue *queue, u32 intid, u8 priority);
bool hv_timer_delivery_deassert(struct hv_timer_delivery_queue *queue, u32 intid);
bool hv_timer_delivery_contains(const struct hv_timer_delivery_queue *queue, u32 intid);
bool hv_timer_delivery_pop(struct hv_timer_delivery_queue *queue,
                           struct hv_timer_delivery *out);

#endif
