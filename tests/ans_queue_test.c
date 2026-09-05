/* SPDX-License-Identifier: MIT */
#include "../src/ans_queue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct nvme_command sq[2][64];
static struct nvme_completion cq[2][64];
static struct apple_nvmmu_tcb tcb[2][64];
static struct ans_queue_engine e;
static char events[4096];
static unsigned n, expected_queue, expected_tag;
static bool invalidation_ok;
static bool publish_result_on_barrier;
static void event(char c) { events[n++] = c; }
static void wmb(void *ctx)
{
    assert(ctx == &e);
    assert(sq[expected_queue][expected_tag].opcode == 2);
    assert(sq[expected_queue][expected_tag].tag == expected_tag);
    assert(tcb[expected_queue][expected_tag].prp1 == UINT64_C(0xf000000000001000));
    assert(tcb[expected_queue][expected_tag].slot_id == expected_tag);
    event('W');
}
static void rmb(void *ctx)
{
    assert(ctx == &e);
    if (publish_result_on_barrier)
        cq[1][e.queues[1].head].result = UINT64_C(0x123456789abcdef0);
    event('R');
}
static void submit(void *ctx, unsigned q, unsigned tag)
{
    assert(ctx == &e && events[n - 1] == 'W');
    assert(sq[q][tag].tag == tag && tcb[q][tag].slot_id == tag);
    assert(e.outstanding & (UINT64_C(1) << tag));
    event('S');
}
static bool invalidate(void *ctx, unsigned tag)
{
    assert(ctx == &e && tag < 64 && events[n - 1] == 'R');
    event('I');
    return invalidation_ok;
}
static void ack(void *ctx, unsigned q, unsigned head)
{
    assert(ctx == &e && head == e.queues[q].head && events[n - 1] == 'I');
    assert(e.outstanding); /* no reuse in invalidation-to-ack interval */
    event('A');
}
static void setup(bool recovery)
{
    struct ans_hw_queue queues[2] = {
        {.cmds=sq[0], .cqes=cq[0], .tcbs=tcb[0]},
        {.cmds=sq[1], .cqes=cq[1], .tcbs=tcb[1]}
    };
    struct ans_queue_ops ops = {wmb, rmb, submit, invalidate, ack};
    memset(&e, 0, sizeof(e));
    memset(cq, 0, sizeof(cq));
    memset(sq, 0, sizeof(sq));
    memset(tcb, 0, sizeof(tcb));
    n = 0; invalidation_ok = true; publish_result_on_barrier = false;
    assert(ans_queue_init(&e, queues, &ops, &e, 2, recovery) == ANS_Q_OK);
}
static struct nvme_command command = {
    .opcode=2, .prp1=UINT64_C(0xf000000000001000),
    .prp2=UINT64_C(0xf000000000009000), .cdw12=31, .tag=255, .rsvd=255
};
static int put(unsigned q, unsigned tag)
{
    expected_queue=q; expected_tag=tag;
    return ans_queue_submit(&e, e.generation, q, tag, &command, 0, 1);
}
static int complete(unsigned q, unsigned tag, unsigned status)
{
    cq[q][e.queues[q].head] = (struct nvme_completion){
        .tag=tag, .status=(status << 1) | e.queues[q].phase,
        .result=0x123456789abcdef0, .rsvd=0xffffffff
    };
    struct nvme_completion out;
    int ret = ans_queue_poll(&e, e.generation, q, &out);
    if (ret == ANS_Q_OK) {
        assert(out.tag == tag && (out.status >> 1) == status);
        assert(out.result == UINT64_C(0x123456789abcdef0));
    }
    return ret;
}
int main(void)
{
    /* Invariant: a shared tag can never identify two outstanding DMA requests. */
    setup(false);
    assert(put(0, 2) == ANS_Q_INVALID);
    assert(put(1, 0) == ANS_Q_INVALID);
    assert(put(1, 64) == ANS_Q_INVALID);
    assert(put(2, 2) == ANS_Q_INVALID);
    assert(put(0, 0) == ANS_Q_OK);
    for (unsigned tag=2; tag<64; tag++) assert(put(1, tag) == ANS_Q_OK);
    assert(put(1, 2) == ANS_Q_BUSY);
    assert(tcb[1][2].opcode == 0 && tcb[1][2].dma_flags == 1);
    assert(tcb[1][2].len == 31 && tcb[1][2].prp1 == command.prp1);
    assert(tcb[1][2].prp2 == command.prp2 && sq[1][2].rsvd == 0);
    /* Invalid/unmapped payload addresses above are never dereferenced. */
    assert(ans_queue_reset(&e, false) == ANS_Q_BUSY);
    for (unsigned tag=63; tag>=2; tag--) assert(complete(1, tag, tag == 3 ? 7 : 0) == 0);
    assert(complete(0, 0, 0) == 0 && !e.outstanding);
    assert(complete(1, 2, 0) == ANS_Q_FAULT); /* duplicate */
    assert(put(1, 2) == ANS_Q_FAULT);
    assert(ans_queue_reset(&e, false) == ANS_Q_BUSY);
    uint64_t old = e.generation;
    assert(ans_queue_reset(&e, true) == ANS_Q_OK && e.generation == old + 1);
    assert(ans_queue_submit(&e, old, 1, 2, &command, 0, 1) == ANS_Q_STALE);
    struct nvme_completion out;
    assert(ans_queue_poll(&e, old, 1, &out) == ANS_Q_STALE);
    assert(ans_queue_poll(&e, e.generation, 1, &out) == ANS_Q_EMPTY);
    /* Invariant: phase wraps after exactly 64 entries; old entries stay empty. */
    n=0;
    for (unsigned i=0; i<130; i++) {
        assert(put(1, 2) == 0);
        assert(complete(1, 2, 0) == 0);
        assert(e.queues[1].head == (i+1)%64);
        assert(e.queues[1].phase == (1 ^ (((i+1)/64)&1)));
        assert(ans_queue_poll(&e, e.generation, 1, &out) == ANS_Q_EMPTY);
    }
    /* Wrong source queue never invalidates another queue's live tag. */
    setup(false); assert(put(1, 2) == 0);
    assert(complete(0, 2, 0) == ANS_Q_FAULT && e.outstanding == 4);
    assert(e.queues[0].head == 0 && events[n-1] == 'R');
    assert(ans_queue_reset(&e, false) == ANS_Q_BUSY);
    assert(ans_queue_reset(&e, true) == 0 && !e.outstanding);
    /* Invalidation failure retains backing/tag and never acknowledges CQ. */
    assert(put(1, 2) == 0); invalidation_ok=false;
    assert(complete(1, 2, 0) == ANS_Q_FAULT && e.outstanding == 4);
    assert(e.queues[1].head == 0 && events[n-1] == 'I');
    assert(ans_queue_reset(&e, true) == 0);
    e.generation=UINT64_MAX;
    assert(ans_queue_reset(&e, true) == ANS_Q_FAULT && e.generation == UINT64_MAX);
    setup(false); assert(put(1, 2) == 0);
    assert(complete(1, 65535, 0) == ANS_Q_FAULT && e.outstanding == 4);
    assert(events[n-1] == 'R');
    /* CQ data must be sampled after the ownership-dependent DMA barrier. */
    setup(false); assert(put(1, 2) == 0);
    cq[1][0] = (struct nvme_completion){.tag=2, .status=1};
    publish_result_on_barrier=true;
    assert(ans_queue_poll(&e, e.generation, 1, &out) == 0);
    assert(out.result == UINT64_C(0x123456789abcdef0));
    assert(put(1, 2) == 0);
    ans_queue_fault(&e); /* timeout keeps tag/backing pinned */
    assert(e.outstanding == 4 && put(1, 3) == ANS_Q_FAULT);
    assert(ans_queue_reset(&e, false) == ANS_Q_BUSY);
    /* Recovery keeps tag0 for either queue but never concurrently. */
    setup(true); assert(put(0, 0) == 0);
    assert(put(1, 0) == ANS_Q_BUSY && put(1, 1) == ANS_Q_INVALID);
    assert(complete(0, 0, 0) == 0 && put(1, 0) == 0);
    assert(complete(1, 0, 0) == 0);
    puts("ans_queue_test: ok");
}
