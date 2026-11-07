/* SPDX-License-Identifier: MIT */
#include "hv_ans_service.h"
#include "hv_ans_direct.h"
#include <string.h>
#include <stddef.h>

/* Counters cover native service lifetime across generations. Starts/stops/resets
 * count admitted backend attempts, including failures; inherited recovery boot
 * and the one-time handoff are outside this native accounting interval.
 * Unknown publication permanently invalidates affected cumulative fields. */
#define SERVICE_FIELDS (ANS_TELEM_STARTS | ANS_TELEM_STOPS | ANS_TELEM_RESETS | \
    ANS_TELEM_SUBMITTED | ANS_TELEM_COMPLETED | ANS_TELEM_READS | ANS_TELEM_WRITES | \
    ANS_TELEM_FLUSHES | ANS_TELEM_BYTES_READ | ANS_TELEM_BYTES_WRITTEN | \
    ANS_TELEM_MAX_OUTSTANDING | ANS_TELEM_CURRENT_OUTSTANDING | ANS_TELEM_CQES | \
    ANS_TELEM_BROKER_REJECTIONS | ANS_TELEM_PRP_TRANSLATION_FAILURES)
#define PUBLICATION_FIELDS (ANS_TELEM_SUBMITTED | ANS_TELEM_COMPLETED | ANS_TELEM_READS | \
    ANS_TELEM_WRITES | ANS_TELEM_FLUSHES | ANS_TELEM_BYTES_READ | ANS_TELEM_BYTES_WRITTEN | \
    ANS_TELEM_MAX_OUTSTANDING | ANS_TELEM_CURRENT_OUTSTANDING)
static void metric(struct ans_service *s, uint64_t bit, size_t offset, uint64_t n)
{
    uint64_t *value=(uint64_t *)((uint8_t *)&s->telemetry+offset);
    if (!(s->telemetry.valid_fields & bit)) return;
    if (*value > UINT64_MAX-n) { s->telemetry.valid_fields &= ~bit; return; }
    *value += n;
}
#define ADD(s,bit,field,n) metric(s,ANS_TELEM_##bit,offsetof(struct ans_telemetry,field),n)
void ans_service_record_rejection(struct ans_service *s, uint32_t result)
{
    if(!result) return;
    if(s->rejected!=UINT64_MAX) s->rejected++;
    ADD(s,BROKER_REJECTIONS,broker_rejections,1);
    s->broker.header.last_error=result;
}
void ans_service_telemetry_snapshot(const struct ans_service *s, struct ans_telemetry *out)
{
    *out=s->telemetry;
    out->generation=s->broker.header.generation;
    out->last_stage=s->last_stage;out->last_error=s->broker.header.last_error;
}


static uint64_t le(const uint8_t *p, unsigned n)
{ uint64_t v=0; for(unsigned i=0;i<n;i++) v|=(uint64_t)p[i]<<(8*i); return v; }
static bool zero(const void *p, size_t n)
{ const uint8_t*b=p;while(n--)if(*b++)return false;return true; }
static int fault(struct ans_service *s)
{ s->broker.header.state=ANS_STATE_FAULTED;s->broker.header.last_error=ANS_RESULT_IO_ERROR;return ANS_RESULT_IO_ERROR; }

bool ans_service_init(struct ans_service *s,const struct ans_service_ops *o,void *ctx,
                      const struct ans_memory_ops *m,void *mc,uint64_t blocks)
{
    if(!s||!o||!m||!blocks||!o->freeze_recovery||!o->drain_recovery||!o->flush_recovery||
       !o->stop||!o->retire||!o->hide_recovery||!o->boot_admin||!o->queue_backing||
       !o->submit||!o->poll||!o->shadow_list||!o->release_tag||!m->translate||!m->read_metadata)
        return false;
    memset(s,0,sizeof(*s));ans_broker_init(&s->broker);
    s->telemetry=(struct ans_telemetry){.magic=ANS_TELEMETRY_MAGIC,
        .size=sizeof(struct ans_telemetry),.source=2,.valid_fields=SERVICE_FIELDS};
    s->ops=*o;s->ctx=ctx;s->memory=*m;s->memory_ctx=mc;s->namespace_blocks=blocks;
    return true;
}

bool ans_service_set_write_window(struct ans_service *s,uint64_t first,uint64_t count)
{
    if(!s||s->handoff_started||s->direct||s->write_window.count||s->broker.owner!=ANS_OWNER_RECOVERY||
       !count||first>=s->namespace_blocks||count>s->namespace_blocks-first)
        return false;
    s->write_window=(struct ans_write_window){first,count};
    s->broker.header.features=(s->broker.header.features&~(uint32_t)ANS_FEATURE_READ_ONLY)|
                              ANS_FEATURE_WRITE_WINDOW;
    return true;
}

bool ans_service_set_direct(struct ans_service *s)
{
    if(!s||s->handoff_started||s->direct||s->write_window.count||!s->ops.direct_release||
       s->broker.owner!=ANS_OWNER_RECOVERY)
        return false;
    s->direct=true;return true;
}

void ans_service_handoff(struct ans_service *s,uint64_t sequence)
{
    if(!s||!sequence)return;
    /* A duplicate transaction may read its frozen receipt, never repeat teardown.
     * A different sequence cannot revive a partially failed ownership transfer. */
    if(s->handoff_started)return;
    s->handoff_started=true;
    struct ans_handoff_receipt *r=&s->handoff;
    r->magic=ANS_MAGIC;r->abi_major=ANS_ABI_MAJOR;r->abi_minor=ANS_ABI_MINOR;
    r->size=sizeof(*r);r->sequence=sequence;r->previous_generation=s->broker.header.generation;
    r->generation=r->previous_generation;r->result=ANS_RESULT_IO_ERROR;
    uint32_t stage=0;
    if(!s->ops.freeze_recovery(s->ctx))goto failed;
    r->proof_flags|=1;
    if(!s->ops.drain_recovery(s->ctx))goto failed;
    r->proof_flags|=2;
    if(!s->ops.flush_recovery(s->ctx))goto failed;
    r->proof_flags|=4;
    if(!s->ops.stop(s->ctx,&stage))goto failed;
    r->proof_flags|=8|16;
    if(!s->ops.retire(s->ctx)||!s->ops.hide_recovery(s->ctx))goto failed;
    r->proof_flags|=32;
    if(s->direct) {
        /* Physical release happens before the receipt can claim success; a failed
         * cold boot leaves a FAULTED receipt and the Mu gate dead-loops pre-EBS. */
        if(!s->ops.direct_release(s->ctx,&stage)){s->last_stage=stage;goto failed;}
        s->last_stage=stage;
    }
    if(ans_broker_handoff(&s->broker,true,true,true,true))goto failed;
    if(s->direct) {
        s->broker.owner=ANS_OWNER_DIRECT;
        s->broker.header.reserved=ANS_DIRECT_HEADER_MARKER;
    }
    r->proof_flags|=64;r->result=ANS_RESULT_OK;
    r->generation=s->broker.header.generation;r->state=s->broker.header.state;
    return;
failed:
    fault(s);r->state=ANS_STATE_FAULTED;
}

int ans_service_drain(struct ans_service *s)
{
    if(!s||s->broker.owner!=ANS_OWNER_NATIVE || !s->broker.queues_registered)return ANS_RESULT_BAD_STATE;
    if(s->broker.header.state==ANS_STATE_FAULTED)return ANS_RESULT_IO_ERROR;
    for(unsigned q=0;q<2;q++) {
        if(q && !s->io_cq)continue;
        /* At most one ring of completions per pass; bounded even under corruption. */
        for(unsigned i=0;i<ANS_QUEUE_DEPTH;i++) {
            struct nvme_completion c;
            int rc=s->ops.poll(s->ctx,s->broker.header.generation,q,&c);
            if(rc==ANS_Q_EMPTY)break;
            if(rc!=ANS_Q_OK)return fault(s);
            if(ans_broker_complete(&s->broker,s->broker.header.generation,q,c.tag,(uint8_t*)&c))
                return fault(s);
            s->pending|=UINT64_C(1)<<c.tag;s->completed++;
            ADD(s,CQES,cqes,1);ADD(s,COMPLETED,completed,1);
            if(s->telemetry.valid_fields & ANS_TELEM_CURRENT_OUTSTANDING) {
                if(s->telemetry.current_outstanding) s->telemetry.current_outstanding--;
                else s->telemetry.valid_fields &= ~(ANS_TELEM_CURRENT_OUTSTANDING | ANS_TELEM_MAX_OUTSTANDING);
            }
            if(q==1 && !(c.status>>1)) {
                if(s->opcode[c.tag]==2) ADD(s,BYTES_READ,bytes_read,s->transfer_bytes[c.tag]);
                if(s->opcode[c.tag]==1) ADD(s,BYTES_WRITTEN,bytes_written,s->transfer_bytes[c.tag]);
            }
            if(q==0 && (s->opcode[c.tag]==5 || s->opcode[c.tag]==1)) {
                bool ok=!(c.status & 0xffe);
                if(s->opcode[c.tag]==5){s->io_cq=ok;s->io_cq_pending=false;}
                else {s->io_sq=ok;s->io_sq_pending=false;}
                /* Do not retry queue creation against an unproven controller
                 * state. The actual error CQE remains available for REAP. */
                if(!ok){fault(s);return ANS_RESULT_OK;}
            }
        }
    }
    return ANS_RESULT_OK;
}

static int admin_command(struct ans_service *s,const struct ans_broker_request*r,
                         struct nvme_command*c,uint32_t*direction)
{
    if(r->tag>=ANS_ADMIN_TAGS||le(r->sqe+2,2)!=r->tag)return ANS_RESULT_BAD_TAG;
    if(c->flags||c->rsvd||c->cdw2||c->cdw3||c->metadata||c->cdw12||c->cdw13||c->cdw14||c->cdw15)
        return ANS_RESULT_BAD_COMMAND;
    switch(c->opcode) {
    case 6: /* Identify Controller / Namespace1 only. */
        if(c->cdw11||!((c->nsid==0&&c->cdw10==1)||(c->nsid==1&&c->cdw10==0))||
           r->transfer_length!=4096||r->direction!=ANS_DMA_FROM_DEVICE)
            return ANS_RESULT_BAD_COMMAND;
        *direction=ANS_DMA_FROM_DEVICE;return ANS_RESULT_OK;
    case 5: case 1: {
        uint64_t sq,cq;
        if(c->nsid||c->prp1||c->prp2||r->transfer_length||r->direction||
           c->cdw10!=((ANS_QUEUE_DEPTH-1u)<<16|1u))return ANS_RESULT_BAD_COMMAND;
        if(c->opcode==5 ? (s->io_cq||s->io_cq_pending||c->cdw11!=3) : (!s->io_cq||s->io_sq||s->io_sq_pending||c->cdw11!=0x10001))
            return ANS_RESULT_BAD_STATE;
        if(!s->ops.queue_backing(s->ctx,1,&sq,&cq)||!sq||!cq)return ANS_RESULT_IO_ERROR;
        c->prp1=c->opcode==5?cq:sq;
        /* Logical V1 queue metadata is EL2-owned; NVMMU direction follows the
         * actual odd admin opcode, not guest transfer_length=0. */
        *direction=ANS_DMA_TO_DEVICE;return ANS_RESULT_OK;
    }
    case 0x0a: /* Current Volatile Write Cache feature, no payload. */
        if(c->nsid||c->prp1||c->prp2||c->cdw10!=6||c->cdw11||r->transfer_length||r->direction)
            return ANS_RESULT_BAD_COMMAND;
        *direction=ANS_DMA_NONE;return ANS_RESULT_OK;
    default:return ANS_RESULT_DENIED;
    }
}

static int authorize(struct ans_service*s,const struct ans_broker_request*r)
{
    if(s->broker.owner!=ANS_OWNER_NATIVE || s->broker.header.state!=ANS_STATE_CLAIMED ||
       !s->broker.queues_registered)return ANS_RESULT_BAD_STATE;
    struct nvme_command c;
    memcpy(&c,r->sqe,sizeof(c));
    uint32_t direction=r->direction;
    int rc;
    if(r->queue>1)return ANS_RESULT_BAD_QUEUE;
    if(r->queue==0)rc=admin_command(s,r,&c,&direction);
    else {
        if(!s->io_sq)return ANS_RESULT_BAD_STATE;
        rc=ans_broker_validate_io(r->sqe,r->tag,r->transfer_length,r->direction,s->namespace_blocks,&s->write_window);
    }
    if(rc)return rc;
    rc=ans_broker_prepare(&s->broker,r->queue,r->tag);
    if(rc)return rc;
    if(r->transfer_length) {
        struct ans_prp_shadow p;
        rc=ans_prp_translate(&s->memory,s->memory_ctx,c.prp1,c.prp2,r->transfer_length,r->direction,&p);
        if(rc){ADD(s,PRP_TRANSLATION_FAILURES,prp_translation_failures,1);goto reject;}
        c.prp1=p.host_pages[0];c.prp2=0;
        if(p.page_count==2)c.prp2=p.host_pages[1];
        if(p.page_count>2 && (!s->ops.shadow_list(s->ctx,r->tag,p.host_pages+1,p.page_count-1,&c.prp2)||
                             !c.prp2||(c.prp2&(ANS_PRP_PAGE_SIZE-1)))) {
            rc=ANS_RESULT_IO_ERROR;goto reject;
        }
    }
    s->opcode[r->tag]=c.opcode;s->transfer_bytes[r->tag]=r->transfer_length;
    rc=ans_broker_submit(&s->broker,r->queue,r->tag);
    if(rc)goto reject;
    /* From this point an error is uncertain: retain tag, shadows and pins. */
    if(s->ops.submit(s->ctx,s->broker.header.generation,r->queue,r->tag,&c,direction)!=ANS_Q_OK) {
        s->telemetry.valid_fields &= ~PUBLICATION_FIELDS;
        return fault(s);
    }
    ADD(s,SUBMITTED,submitted,1);ADD(s,CURRENT_OUTSTANDING,current_outstanding,1);
    if(!(s->telemetry.valid_fields & ANS_TELEM_CURRENT_OUTSTANDING))
        s->telemetry.valid_fields &= ~ANS_TELEM_MAX_OUTSTANDING;
    else if(s->telemetry.current_outstanding>s->telemetry.max_outstanding)
        s->telemetry.max_outstanding=s->telemetry.current_outstanding;
    if(r->queue==1) {
        if(c.opcode==2) ADD(s,READS,reads,1);
        if(c.opcode==1) ADD(s,WRITES,writes,1);
        if(c.opcode==0) ADD(s,FLUSHES,flushes,1);
    }
    if(r->queue==0 && c.opcode==5)s->io_cq_pending=true;
    if(r->queue==0 && c.opcode==1)s->io_sq_pending=true;
    s->submitted++;return ANS_RESULT_OK;
reject:
    s->ops.release_tag(s->ctx,r->tag);
    memset(&s->broker.tags[r->tag],0,sizeof(s->broker.tags[r->tag]));
    return rc;
}

void ans_service_execute(struct ans_service*s,const struct ans_broker_request*r,
                         struct ans_broker_response*out)
{
    if(!s||!r||!out)return;
    memset(out,0,sizeof(*out));out->sequence=r->sequence;out->queue=r->queue;out->tag=r->tag;
    int rc=ans_broker_request_begin(&s->broker,r);
    uint32_t stage=0;
    if(rc)goto finish;
    /* ANS-DIRECT: the guest owns the physical controller; no broker operation exists. */
    if(s->broker.owner==ANS_OWNER_DIRECT){rc=ANS_RESULT_BAD_STATE;goto finish;}
    if(r->operation!=ANS_OP_AUTHORIZE_SUBMIT &&
       (r->transfer_length||r->direction||!zero(r->sqe,sizeof(r->sqe)))) {
        rc=ANS_RESULT_BAD_COMMAND;goto finish;
    }
    if(r->operation!=ANS_OP_AUTHORIZE_SUBMIT && r->operation!=ANS_OP_REAP_COMPLETION && (r->queue||r->tag)) {
        rc=ANS_RESULT_BAD_COMMAND;goto finish;
    }
    switch(r->operation) {
    case ANS_OP_QUERY:rc=ANS_RESULT_OK;break;
    case ANS_OP_CLAIM:
        rc=ans_broker_claim(&s->broker);break;
    case ANS_OP_REGISTER_QUEUES:
        if(s->broker.owner!=ANS_OWNER_NATIVE || s->broker.header.state!=ANS_STATE_CLAIMED ||
           s->broker.queues_registered || s->admin_started){rc=ANS_RESULT_BAD_STATE;break;}
        ADD(s,STARTS,starts,1);
        s->admin_started=true;s->broker.header.state=ANS_STATE_BOOTING;
        if(!s->ops.boot_admin(s->ctx,s->broker.header.generation,&stage))rc=fault(s);
        else {s->broker.header.state=ANS_STATE_CLAIMED;rc=ans_broker_register_queues(&s->broker);}
        s->last_stage=stage;
        break;
    case ANS_OP_AUTHORIZE_SUBMIT:rc=authorize(s,r);break;
    case ANS_OP_REAP_COMPLETION:
        rc=ans_broker_reap(&s->broker,r->queue,r->tag,out->cqe);
        if(!rc){s->pending&=~(UINT64_C(1)<<r->tag);s->ops.release_tag(s->ctx,r->tag);s->reaped++;}
        break;
    case ANS_OP_QUIESCE:rc=ans_broker_quiesce(&s->broker);break;
    case ANS_OP_RESET:case ANS_OP_RELEASE:
        if(s->broker.owner!=ANS_OWNER_NATIVE ||
           (s->broker.header.state!=ANS_STATE_QUIESCING && s->broker.header.state!=ANS_STATE_FAULTED)){
            rc=ANS_RESULT_BAD_STATE;break;}
        if(r->operation==ANS_OP_RELEASE && s->broker.header.state!=ANS_STATE_QUIESCING){rc=ANS_RESULT_BAD_STATE;break;}
        if(r->operation==ANS_OP_RESET) ADD(s,RESETS,resets,1);
        if(s->admin_started) {
            ADD(s,STOPS,stops,1);
            bool stopped=s->ops.stop(s->ctx,&stage);s->last_stage=stage;
            if(!stopped){rc=fault(s);break;}
        }
        /* No native boot attempt means the full handoff stop proof still holds. */
        for(unsigned t=0;t<ANS_QUEUE_DEPTH;t++)s->ops.release_tag(s->ctx,t);
        if(s->admin_started && !s->ops.retire(s->ctx)){rc=fault(s);break;}
        s->admin_started=false;
        rc=r->operation==ANS_OP_RESET?ans_broker_reset(&s->broker,true):ans_broker_release(&s->broker,true);
        if(!rc){s->telemetry.current_outstanding=0;s->pending=0;s->io_cq=false;s->io_sq=false;s->io_cq_pending=false;s->io_sq_pending=false;}
        break;
    default:rc=ANS_RESULT_BAD_COMMAND;break;
    }
finish:
    if(rc)ans_service_record_rejection(s,(uint32_t)rc);
    s->broker.header.last_error=(uint32_t)rc;
    out->result=(uint32_t)rc;out->generation=s->broker.header.generation;out->state=s->broker.header.state;
}
