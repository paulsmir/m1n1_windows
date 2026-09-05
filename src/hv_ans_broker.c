/* SPDX-License-Identifier: MIT */
#include "hv_ans_broker.h"
#include <string.h>
#include <limits.h>

static int next_generation(struct ans_broker *b)
{
    if (b->header.generation == UINT64_MAX) {
        b->header.state = ANS_STATE_FAULTED;
        return ANS_RESULT_STALE;
    }
    b->header.generation++;
    b->header.sequence = 0;
    return ANS_RESULT_OK;
}

void ans_broker_init(struct ans_broker *b)
{
    memset(b, 0, sizeof(*b));
    b->header.magic = ANS_MAGIC;
    b->header.abi_major = ANS_ABI_MAJOR;
    b->header.abi_minor = ANS_ABI_MINOR;
    b->header.size = sizeof(b->header);
    b->header.features = ANS_FEATURE_READ_ONLY;
    b->header.controller_generation = 2;
    b->owner = ANS_OWNER_RECOVERY;
}

int ans_broker_handoff(struct ans_broker *b, bool drained, bool flushed,
                       bool disabled, bool rtkit_stopped)
{
    if (b->owner != ANS_OWNER_RECOVERY || b->header.state != ANS_STATE_OFF)
        return ANS_RESULT_BAD_STATE;
    if (!drained || !flushed || !disabled || !rtkit_stopped)
        return ANS_RESULT_IO_ERROR;
    int rc = next_generation(b);
    if (rc) return rc;
    b->owner = ANS_OWNER_NONE;
    b->header.state = ANS_STATE_READY;
    return ANS_RESULT_OK;
}

int ans_broker_request_begin(struct ans_broker *b, const struct ans_broker_request *r)
{
    if (r->abi_major != ANS_ABI_MAJOR || r->abi_minor != ANS_ABI_MINOR ||
        r->size != sizeof(*r) || r->reserved)
        return ANS_RESULT_BAD_ABI;
    if (r->generation != b->header.generation) return ANS_RESULT_STALE;
    if (!r->sequence || r->sequence <= b->header.sequence) return ANS_RESULT_BAD_SEQUENCE;
    if (r->operation <= ANS_OP_INVALID || r->operation > ANS_OP_RELEASE)
        return ANS_RESULT_BAD_COMMAND;
    b->header.sequence = r->sequence;
    return ANS_RESULT_OK;
}

int ans_broker_claim(struct ans_broker *b)
{
    if (b->owner != ANS_OWNER_NONE || b->header.state != ANS_STATE_READY)
        return ANS_RESULT_BAD_STATE;
    b->owner = ANS_OWNER_NATIVE;
    b->header.state = ANS_STATE_CLAIMED;
    return ANS_RESULT_OK;
}

int ans_broker_register_queues(struct ans_broker *b)
{
    if (b->owner != ANS_OWNER_NATIVE || b->header.state != ANS_STATE_CLAIMED ||
        b->queues_registered) return ANS_RESULT_BAD_STATE;
    b->queues_registered = true;
    return ANS_RESULT_OK;
}

static int tag_check(uint16_t queue, uint16_t tag)
{
    if (queue > 1) return ANS_RESULT_BAD_QUEUE;
    if (tag >= ANS_QUEUE_DEPTH || (queue == 0 ? tag >= ANS_ADMIN_TAGS : tag < ANS_ADMIN_TAGS))
        return ANS_RESULT_BAD_TAG;
    return ANS_RESULT_OK;
}

static int transition(struct ans_broker *b, uint16_t q, uint16_t tag, uint8_t from, uint8_t to)
{
    int rc = tag_check(q, tag);
    if (rc) return rc;
    if (b->owner != ANS_OWNER_NATIVE || !b->queues_registered || b->tags[tag].state != from)
        return ANS_RESULT_BAD_STATE;
    if (from != ANS_TAG_STATE_FREE && b->tags[tag].queue != q) return ANS_RESULT_BAD_QUEUE;
    b->tags[tag].queue = (uint8_t)q;
    b->tags[tag].state = to;
    return ANS_RESULT_OK;
}

int ans_broker_prepare(struct ans_broker *b, uint16_t q, uint16_t tag)
{
    if (b->header.state != ANS_STATE_CLAIMED) return ANS_RESULT_BAD_STATE;
    return transition(b,q,tag,ANS_TAG_STATE_FREE,ANS_TAG_STATE_PREPARED);
}

int ans_broker_submit(struct ans_broker *b, uint16_t q, uint16_t tag)
{
    if (b->header.state != ANS_STATE_CLAIMED) return ANS_RESULT_BAD_STATE;
    return transition(b,q,tag,ANS_TAG_STATE_PREPARED,ANS_TAG_STATE_IN_FLIGHT);
}

int ans_broker_complete(struct ans_broker *b, uint64_t generation, uint16_t q,
                        uint16_t tag, const uint8_t cqe[16])
{
    if (generation != b->header.generation) return ANS_RESULT_STALE;
    /* ANS DWORD2 is reserved in current nvme.c. Pinned Linux apple_nvme_handle_cqe
     * also uses the source CQ, never CQE SQID. Validate trusted source q and CID;
     * do not impose a generic PCI NVMe SQID invariant on this ANS boundary. */
    if (!cqe || ((uint16_t)cqe[12] | ((uint16_t)cqe[13] << 8)) != tag)
        return ANS_RESULT_BAD_TAG;
    int rc = transition(b,q,tag,ANS_TAG_STATE_IN_FLIGHT,ANS_TAG_STATE_COMPLETED);
    if (!rc) memcpy(b->tags[tag].cqe,cqe,16);
    return rc;
}

int ans_broker_reap(struct ans_broker *b, uint16_t q, uint16_t tag, uint8_t cqe[16])
{
    if (!cqe) return ANS_RESULT_BAD_COMMAND;
    int rc = transition(b,q,tag,ANS_TAG_STATE_COMPLETED,ANS_TAG_STATE_FREE);
    if (!rc) { memcpy(cqe,b->tags[tag].cqe,16); memset(&b->tags[tag],0,sizeof(b->tags[tag])); }
    return rc;
}

int ans_broker_quiesce(struct ans_broker *b)
{
    if (b->owner != ANS_OWNER_NATIVE || b->header.state != ANS_STATE_CLAIMED)
        return ANS_RESULT_BAD_STATE;
    b->header.state = ANS_STATE_QUIESCING;
    return ANS_RESULT_OK;
}

int ans_broker_reset(struct ans_broker *b, bool dma_stopped)
{
    if (!dma_stopped) return ANS_RESULT_IO_ERROR;
    if (b->owner != ANS_OWNER_NATIVE || (b->header.state != ANS_STATE_QUIESCING &&
        b->header.state != ANS_STATE_FAULTED)) return ANS_RESULT_BAD_STATE;
    int rc = next_generation(b);
    if (rc) return rc;
    memset(b->tags,0,sizeof(b->tags));
    b->queues_registered = false;
    b->owner = ANS_OWNER_NONE;
    b->header.state = ANS_STATE_READY;
    return ANS_RESULT_OK;
}

int ans_broker_release(struct ans_broker *b, bool dma_stopped)
{
    int rc = ans_broker_reset(b,dma_stopped);
    if (!rc) b->header.state = ANS_STATE_OFF;
    return rc;
}

static uint64_t le(const uint8_t *p, size_t n)
{
    uint64_t v=0;
    for (size_t i=0;i<n;i++) v |= (uint64_t)p[i] << (8*i);
    return v;
}

int ans_broker_validate_io(const uint8_t sqe[64], uint16_t tag, uint32_t length,
                           uint32_t direction, uint64_t blocks, bool read_only)
{
    if (!sqe) return ANS_RESULT_BAD_COMMAND;
    if (tag_check(1,tag) || le(sqe+2,2)!=tag) return ANS_RESULT_BAD_TAG;
    if (sqe[1] || sqe[3] || le(sqe+4,4)!=1 || le(sqe+8,8) || le(sqe+16,8) ||
        le(sqe+52,8) || le(sqe+60,4)) return ANS_RESULT_BAD_COMMAND;
    if (sqe[0] == 0) { /* FLUSH: no payload, but forbidden in early RO qualification. */
        if (read_only) return ANS_RESULT_DENIED;
        return (!length && direction==ANS_DMA_NONE && !le(sqe+24,8) &&
                !le(sqe+32,8) && !le(sqe+40,8) && !le(sqe+48,4)) ?
                ANS_RESULT_OK : ANS_RESULT_BAD_COMMAND;
    }
    if (sqe[0] != 1 && sqe[0] != 2) return ANS_RESULT_BAD_COMMAND;
    if (sqe[0]==1 && read_only) return ANS_RESULT_DENIED;
    uint32_t expected = sqe[0]==1 ? ANS_DMA_TO_DEVICE : ANS_DMA_FROM_DEVICE;
    uint64_t count=le(sqe+48,2)+1, lba=le(sqe+40,8);
    /* V1 permits NLB and FUA only. Protection, LR and reserved flags fail closed. */
    uint32_t flags=(uint32_t)le(sqe+48,4);
    if (flags & 0xbfff0000u) return ANS_RESULT_BAD_COMMAND;
    if (direction != expected || !length || length>ANS_MAX_TRANSFER ||
        count*4096!=length || lba>=blocks || count>blocks-lba)
        return ANS_RESULT_BAD_COMMAND;
    return ANS_RESULT_OK;
}
