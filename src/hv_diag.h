/* SPDX-License-Identifier: MIT */

#ifndef HV_DIAG_H
#define HV_DIAG_H

#ifdef HV_DIAG_HOST_TEST
#include <stdbool.h>
#include <stdint.h>
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include "types.h"
#endif

#define HV_DIAG_ABI_VERSION   1u
#define HV_DIAG_RING_CAPACITY 256u
#define HV_DIAG_QUEUE_COUNT   2u
#define HV_DIAG_J313_XHCI_HW_IRQ 857u
#define HV_DIAG_J313_XHCI_VINTID 857u
#define HV_DIAG_SAMPLE_TICKS 25000u

struct exc_info;

enum hv_diag_sample_flags {
    HV_DIAG_FLAG_NVME_READY = 1u << 0,
    HV_DIAG_FLAG_NVME_IRQ_ASSERTED = 1u << 1,
    HV_DIAG_FLAG_FB_ENABLED = 1u << 2,
};

enum hv_diag_counter {
    HV_DIAG_NVME_IRQ_INJECT,
    HV_DIAG_NVME_IRQ_IAR,
    HV_DIAG_NVME_IRQ_EOI,
    HV_DIAG_XHCI_HW_IRQ,
    HV_DIAG_XHCI_IRQ_INJECT,
    HV_DIAG_XHCI_IRQ_IAR,
    HV_DIAG_XHCI_IRQ_EOI,
    HV_DIAG_COUNTER_COUNT,
};

enum hv_diag_irq_stage {
    HV_DIAG_IRQ_INJECT,
    HV_DIAG_IRQ_IAR,
    HV_DIAG_IRQ_EOI,
};

struct hv_diag_queue_v1 {
    u16 sq_head;
    u16 sq_tail;
    u16 cq_head;
    u16 cq_tail;
};

struct hv_diag_sample_v1 {
    u64 sequence;
    u64 host_fiq_count;
    u64 host_tick_count;
    u64 guest_pc;
    u64 guest_spsr;
    u64 nvme_sq_doorbells;
    u64 nvme_cq_doorbells;
    u64 nvme_commands;
    u64 nvme_completions;
    u64 nvme_irq_injects;
    u64 nvme_irq_iars;
    u64 nvme_irq_eois;
    u64 xhci_hw_irqs;
    u64 xhci_irq_injects;
    u64 xhci_irq_iars;
    u64 xhci_irq_eois;
    u64 fb_completed_frames;
    u64 fb_backpressure_skips;
    u32 vgic_pending_lrs;
    u32 vgic_active_lrs;
    u32 vgic_occupied_lrs;
    u32 flags;
    struct hv_diag_queue_v1 queues[HV_DIAG_QUEUE_COUNT];
};

struct hv_diag_status_v1 {
    u32 abi_version;
    u32 sample_size;
    u32 capacity;
    u32 count;
    u64 oldest_sequence;
    u64 next_sequence;
};

typedef void (*hv_diag_collect_fn)(void *opaque, const struct exc_info *ctx,
                                   struct hv_diag_sample_v1 *sample);

_Static_assert((HV_DIAG_RING_CAPACITY & (HV_DIAG_RING_CAPACITY - 1)) == 0,
               "diagnostic ring capacity must be a power of two");
_Static_assert(sizeof(struct hv_diag_queue_v1) == 8, "diagnostic queue ABI size");
_Static_assert(sizeof(struct hv_diag_sample_v1) == 176, "diagnostic sample ABI size");
_Static_assert(sizeof(struct hv_diag_status_v1) == 32, "diagnostic status ABI size");

void hv_diag_reset(void);
void hv_diag_publish(const struct hv_diag_sample_v1 *sample);
bool hv_diag_get_status(struct hv_diag_status_v1 *out);
bool hv_diag_get_sample(u64 sequence, struct hv_diag_sample_v1 *out);
void hv_diag_count(enum hv_diag_counter counter);
void hv_diag_get_counters(u64 out[HV_DIAG_COUNTER_COUNT]);
void hv_diag_count_hw_irq(u32 hw_irq);
void hv_diag_count_vgic_irq(enum hv_diag_irq_stage stage, u32 vintid, u32 nvme_vintid);
void hv_diag_set_collector(hv_diag_collect_fn collect, void *opaque);
void hv_diag_tick(const struct exc_info *ctx);

#endif
