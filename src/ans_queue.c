/* SPDX-License-Identifier: MIT */
#include "ans_queue.h"
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(struct nvme_command) == 64, "ANS SQE size");
_Static_assert(sizeof(struct nvme_completion) == 16, "ANS CQE size");
_Static_assert(sizeof(struct apple_nvmmu_tcb) == 128, "ANS TCB size");
_Static_assert(offsetof(struct nvme_completion, status) == 14, "ANS CQ phase offset");
_Static_assert(offsetof(struct apple_nvmmu_tcb, prp1) == 24, "ANS TCB PRP offset");

int ans_queue_init(struct ans_queue_engine *e, const struct ans_hw_queue queues[2],
                   const struct ans_queue_ops *ops, void *ctx, unsigned admin_reserved,
                   bool recovery)
{
    if (!e || !queues || !ops || !ops->dma_wmb || !ops->dma_rmb || !ops->submit ||
        !ops->invalidate || !ops->ack || !admin_reserved || admin_reserved >= ANS_HW_DEPTH)
        return ANS_Q_INVALID;
    for (unsigned q = 0; q < ANS_HW_QUEUES; q++)
        if (!queues[q].cmds || !queues[q].cqes || !queues[q].tcbs)
            return ANS_Q_INVALID;
    *e = (struct ans_queue_engine){.ops = *ops, .ctx = ctx, .generation = 1,
                                   .admin_reserved = admin_reserved, .recovery = recovery};
    for (unsigned q = 0; q < ANS_HW_QUEUES; q++) {
        e->queues[q] = queues[q];
        e->queues[q].head = 0;
        e->queues[q].phase = 1;
        memset(e->queues[q].cqes, 0, ANS_HW_DEPTH * sizeof(struct nvme_completion));
    }
    return ANS_Q_OK;
}

static int check(struct ans_queue_engine *e, uint64_t generation, unsigned queue)
{
    if (!e || !e->generation || queue >= ANS_HW_QUEUES)
        return ANS_Q_INVALID;
    if (e->generation != generation)
        return ANS_Q_STALE;
    if (e->faulted)
        return ANS_Q_FAULT;
    return ANS_Q_OK;
}

int ans_queue_submit(struct ans_queue_engine *e, uint64_t generation, unsigned queue,
                     unsigned tag, const struct nvme_command *cmd,
                     uint8_t tcb_opcode, uint8_t dma_flags)
{
    int ret = check(e, generation, queue);
    if (ret)
        return ret;
    if (!cmd || tag >= ANS_HW_DEPTH || (dma_flags & ~3u))
        return ANS_Q_INVALID;
    if (e->recovery ? tag != 0 :
        ((queue == ANS_HW_ADMIN) != (tag < e->admin_reserved)))
        return ANS_Q_INVALID;
    uint64_t bit = UINT64_C(1) << tag;
    if ((e->outstanding & bit) || (e->recovery && e->outstanding))
        return ANS_Q_BUSY;

    struct ans_hw_queue *q = &e->queues[queue];
    /* Copy command metadata only; PRPs have already been authorized by owner. */
    q->cmds[tag] = *cmd;
    q->cmds[tag].tag = tag;
    q->cmds[tag].rsvd = 0;
    q->tcbs[tag] = (struct apple_nvmmu_tcb){
        .opcode = tcb_opcode, .dma_flags = dma_flags, .slot_id = tag,
        .len = e->recovery ? cmd->cdw12 : (uint16_t)cmd->cdw12, .prp1 = cmd->prp1, .prp2 = cmd->prp2
    };
    e->tag_queue[tag] = queue;
    e->outstanding |= bit;
    e->ops.dma_wmb(e->ctx);
    e->ops.submit(e->ctx, queue, tag);
    return ANS_Q_OK;
}

int ans_queue_poll(struct ans_queue_engine *e, uint64_t generation, unsigned queue,
                   struct nvme_completion *out)
{
    int ret = check(e, generation, queue);
    if (ret)
        return ret;
    if (!out)
        return ANS_Q_INVALID;
    struct ans_hw_queue *q = &e->queues[queue];
    volatile struct nvme_completion *entry = &q->cqes[q->head];
    /* Observe ownership first, then order DMA data reads after the phase load. */
    if ((entry->status & 1) != q->phase)
        return ANS_Q_EMPTY;
    e->ops.dma_rmb(e->ctx);
    struct nvme_completion cqe = *entry;
    if ((cqe.status & 1) != q->phase || cqe.tag >= ANS_HW_DEPTH ||
        !(e->outstanding & (UINT64_C(1) << cqe.tag)) ||
        e->tag_queue[cqe.tag] != queue) {
        e->faulted = true;
        return ANS_Q_FAULT;
    }
    if (!e->ops.invalidate(e->ctx, cqe.tag)) {
        e->faulted = true;
        return ANS_Q_FAULT;
    }
    if (++q->head == ANS_HW_DEPTH) {
        q->head = 0;
        q->phase ^= 1;
    }
    e->ops.ack(e->ctx, queue, q->head);
    e->outstanding &= ~(UINT64_C(1) << cqe.tag);
    *out = cqe;
    return ANS_Q_OK;
}

void ans_queue_fault(struct ans_queue_engine *e)
{
    if (e)
        e->faulted = true;
}

int ans_queue_reset(struct ans_queue_engine *e, bool dma_stopped)
{
    if (!e || !e->generation)
        return ANS_Q_INVALID;
    if (!dma_stopped)
        return ANS_Q_BUSY;
    if (e->generation == UINT64_MAX) {
        e->faulted = true;
        return ANS_Q_FAULT;
    }
    e->generation++;
    e->outstanding = 0;
    e->faulted = false;
    memset(e->tag_queue, 0, sizeof(e->tag_queue));
    for (unsigned q = 0; q < ANS_HW_QUEUES; q++) {
        e->queues[q].head = 0;
        e->queues[q].phase = 1;
        memset(e->queues[q].cqes, 0, ANS_HW_DEPTH * sizeof(struct nvme_completion));
    }
    return ANS_Q_OK;
}
