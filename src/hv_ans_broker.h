/* SPDX-License-Identifier: MIT */
#ifndef HV_ANS_BROKER_H
#define HV_ANS_BROKER_H
#include "hv_ans_broker_abi.h"
#include <stdbool.h>
#include <stddef.h>

/* DIRECT: the guest owns the physical controller (ANS-DIRECT); the broker is dead. */
enum ans_owner { ANS_OWNER_RECOVERY, ANS_OWNER_NONE, ANS_OWNER_NATIVE, ANS_OWNER_DIRECT };
struct ans_tag {
    uint8_t state, queue;
    uint8_t cqe[16];
};
struct ans_broker {
    struct ans_broker_header header;
    enum ans_owner owner;
    bool queues_registered;
    struct ans_tag tags[ANS_QUEUE_DEPTH];
};
/* Callers serialize all state operations under the controller lock. No callbacks
 * or guest memory are touched by this deterministic ownership core. */
void ans_broker_init(struct ans_broker *b);
int ans_broker_handoff(struct ans_broker *b, bool drained, bool flushed,
                       bool disabled, bool rtkit_stopped);
int ans_broker_request_begin(struct ans_broker *b, const struct ans_broker_request *r);
int ans_broker_claim(struct ans_broker *b);
int ans_broker_register_queues(struct ans_broker *b);
int ans_broker_prepare(struct ans_broker *b, uint16_t queue, uint16_t tag);
int ans_broker_submit(struct ans_broker *b, uint16_t queue, uint16_t tag);
int ans_broker_complete(struct ans_broker *b, uint64_t generation,
                        uint16_t queue, uint16_t tag, const uint8_t cqe[16]);
int ans_broker_reap(struct ans_broker *b, uint16_t queue, uint16_t tag, uint8_t cqe[16]);
int ans_broker_quiesce(struct ans_broker *b);
int ans_broker_reset(struct ans_broker *b, bool dma_stopped);
int ans_broker_release(struct ans_broker *b, bool dma_stopped);
/* ANS008: LBA range [first, first+count) that NVMe WRITE may target. It is fixed by
 * the host launcher before guest entry and never guest-settable. NULL or count==0
 * keeps the broker read-only; a wrapping window or one past the namespace denies
 * every WRITE. READ and FLUSH are never window-restricted. */
struct ans_write_window { uint64_t first, count; };
int ans_broker_validate_io(const uint8_t sqe[64], uint16_t tag, uint32_t length,
                           uint32_t direction, uint64_t namespace_blocks,
                           const struct ans_write_window *write_window);
#endif
