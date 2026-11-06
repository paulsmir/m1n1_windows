/* SPDX-License-Identifier: MIT */
#ifndef ANS_QUEUE_H
#define ANS_QUEUE_H
#include <stdbool.h>
#include <stdint.h>

#define ANS_HW_DEPTH 64
#define ANS_HW_QUEUES 2
#define ANS_HW_ADMIN 0
#define ANS_HW_IO 1

/* ANS2 wire metadata; payload addresses are opaque to this engine. */
struct nvme_command {
    uint8_t opcode, flags, tag, rsvd;
    uint32_t nsid, cdw2, cdw3;
    uint64_t metadata, prp1, prp2;
    uint32_t cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
};
struct nvme_completion {
    uint64_t result;
    uint32_t rsvd;
    uint16_t tag, status;
};
struct apple_nvmmu_tcb {
    uint8_t opcode, dma_flags, slot_id, unk0;
    uint32_t len;
    uint64_t unk1[2], prp1, prp2, unk2[2];
    uint8_t aes_iv[8], _aes_unk[64];
};
struct ans_hw_queue {
    struct nvme_command *cmds;
    struct nvme_completion *cqes;
    struct apple_nvmmu_tcb *tcbs;
    uint8_t head, phase;
};
struct ans_queue_ops {
    void (*dma_wmb)(void *ctx);
    void (*dma_rmb)(void *ctx);
    void (*submit)(void *ctx, unsigned queue, unsigned tag);
    bool (*invalidate)(void *ctx, unsigned tag);
    void (*ack)(void *ctx, unsigned queue, unsigned head);
};
enum ans_queue_result {
    ANS_Q_OK = 0, ANS_Q_EMPTY = 1, ANS_Q_INVALID = -1,
    ANS_Q_BUSY = -2, ANS_Q_STALE = -3, ANS_Q_FAULT = -4
};
struct ans_queue_engine {
    struct ans_hw_queue queues[ANS_HW_QUEUES];
    struct ans_queue_ops ops;
    void *ctx;
    uint64_t generation;
    uint64_t outstanding;
    uint8_t tag_queue[ANS_HW_DEPTH];
    unsigned admin_reserved;
    bool recovery, faulted;
};
/* All calls and callbacks must be serialized under the same controller lock,
 * with IRQ exclusion. Invalidation through CQ acknowledgement is indivisible
 * with respect to submission (pinned Linux documents missed IRQs otherwise).
 * Backing is trusted, distinct, coherent, controller-owned, 64 entries per array.
 * init is for a fresh object with DMA stopped; never re-init a live object.
 * Native policy reserves [0, admin_reserved); recovery is serialized tag 0.
 * Native caller authorizes commands/PRPs first; this is not a security boundary.
 */
int ans_queue_init(struct ans_queue_engine *e, const struct ans_hw_queue queues[2],
                   const struct ans_queue_ops *ops, void *ctx, unsigned admin_reserved,
                   bool recovery);
int ans_queue_submit(struct ans_queue_engine *e, uint64_t generation, unsigned queue,
                     unsigned tag, const struct nvme_command *cmd,
                     uint8_t tcb_opcode, uint8_t dma_flags);
/* Returns original wire status including phase; tag released only after successful
 * invalidation and CQ ack. Bad/duplicate/wrong-CQ completions fault and pin tags.
 * CQ has no generation: reset receipt MUST exclude all late pre-reset DMA.
 */
/* True when queue has a posted, unconsumed CQE at head. Never consumes or faults. */
bool ans_queue_peek(const struct ans_queue_engine *e, unsigned queue);
int ans_queue_poll(struct ans_queue_engine *e, uint64_t generation, unsigned queue,
                   struct nvme_completion *out);
void ans_queue_fault(struct ans_queue_engine *e);
/* Caller supplies actual controller stop proof, NOT a timeout or logical drain.
 * Outstanding backing cannot be released until this succeeds. Generation never
 * wraps. Clearing CQs is safe only while hardware is stopped. */
int ans_queue_reset(struct ans_queue_engine *e, bool dma_stopped);
#endif
