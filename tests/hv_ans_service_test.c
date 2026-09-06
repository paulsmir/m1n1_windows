#include "hv_ans_service.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
struct fake {
    unsigned step, stops, boots, submits, release, next;
    bool fail_stop, fail_submit;
    struct nvme_command last;
    struct nvme_completion cqe;
};
static bool freeze(void *x) { struct fake*f=x;assert(f->step++==0);return true; }
static bool drain(void *x) { struct fake*f=x;assert(f->step++==1);return true; }
static bool flush(void *x) { struct fake*f=x;assert(f->step++==2);return true; }
static bool stop(void*x,uint32_t*stage){struct fake*f=x;f->stops++;*stage=13;return !f->fail_stop;}
static bool retire(void*x){struct fake*f=x;if(f->boots)assert(f->release>=64);return true;}
static bool hide(void*x){struct fake*f=x;assert(f->step++==3);return true;}
static bool boot(void*x,uint64_t gen,uint32_t*stage){struct fake*f=x;f->boots++;assert(gen);*stage=0;return true;}
static bool backing(void*x,unsigned q,uint64_t*sq,uint64_t*cq){(void)x;assert(q==1);*sq=0x100000;*cq=0x200000;return true;}
static int submit(void*x,uint64_t gen,unsigned q,unsigned t,const struct nvme_command*c,uint32_t d){
 struct fake*f=x;(void)gen;(void)q;(void)t;(void)d;f->last=*c;f->submits++;return f->fail_submit?ANS_Q_FAULT:0;}
static int poll(void*x,uint64_t gen,unsigned q,struct nvme_completion*c){struct fake*f=x;(void)gen;
 if(f->next && q==(f->cqe.tag<ANS_ADMIN_TAGS?0u:1u)){*c=f->cqe;f->next=0;return 0;}return ANS_Q_EMPTY;}
static bool shadow(void*x,unsigned t,const uint64_t*p,unsigned n,uint64_t*pa){(void)x;(void)t;assert(n>1 && p[0]);*pa=0x300000;return true;}
static void release(void*x,unsigned t){struct fake*f=x;(void)t;f->release++;}
static bool translate(void*x,uint64_t ipa,size_t n,uint32_t d,uint64_t*pa){(void)x;(void)n;(void)d;*pa=ipa+0x100000000ULL;return ipa>=0x1000;}
static bool readmeta(void*x,uint64_t pa,void*d,size_t n){(void)x;(void)pa;(void)d;(void)n;return false;}
static struct ans_broker_request req;
static struct ans_broker_response resp;
static void command(struct ans_service*s,unsigned op){if(op!=ANS_OP_AUTHORIZE_SUBMIT){memset(req.sqe,0,64);req.transfer_length=0;req.direction=0;}req.operation=op;req.sequence++;req.generation=s->broker.header.generation;ans_service_execute(s,&req,&resp);}
static void completion(struct ans_service*s,struct fake*f,unsigned tag){f->cqe=(struct nvme_completion){.tag=tag,.status=1};f->next=1;assert(ans_service_drain(s)==0);}
int main(void){
 struct fake f={0}; struct ans_service s;
 struct ans_service_ops ops={freeze,drain,flush,stop,retire,hide,boot,backing,submit,poll,shadow,release};
 struct ans_memory_ops mem={translate,readmeta};
 assert(ans_service_init(&s,&ops,&f,&mem,&f,1000));
 req=(struct ans_broker_request){.abi_major=ANS_ABI_MAJOR,.size=sizeof(req)};
 command(&s,ANS_OP_CLAIM);assert(resp.result==ANS_RESULT_BAD_STATE && !f.boots);
 ans_service_handoff(&s,1);assert(ans_handoff_receipt_valid(&s.handoff,1,0));
 ans_service_handoff(&s,1);assert(f.stops==1 && f.step==4);
 command(&s,ANS_OP_CLAIM);assert(resp.result==0 && f.boots==0);
 command(&s,ANS_OP_REGISTER_QUEUES);assert(resp.result==0 && f.boots==1);
 struct nvme_command c={.opcode=5,.cdw10=(63u<<16)|1,.cdw11=3};
 memcpy(req.sqe,&c,64);command(&s,ANS_OP_AUTHORIZE_SUBMIT);assert(resp.result==0 && f.last.prp1==0x200000);
 c.tag=1;req.tag=1;memcpy(req.sqe,&c,64);command(&s,ANS_OP_AUTHORIZE_SUBMIT);assert(resp.result==ANS_RESULT_BAD_STATE);
 req.tag=0;c.tag=0;
 completion(&s,&f,0);assert(s.pending==1 && s.io_cq);
 command(&s,ANS_OP_REAP_COMPLETION);assert(resp.result==0 && !s.pending);
 c.opcode=1;c.cdw11=0x10001;memcpy(req.sqe,&c,64);command(&s,ANS_OP_AUTHORIZE_SUBMIT);assert(resp.result==0);
 completion(&s,&f,0);command(&s,ANS_OP_REAP_COMPLETION);assert(resp.result==0 && s.io_sq);
 c=(struct nvme_command){.opcode=2,.tag=2,.nsid=1,.prp1=0x4000};
 req.queue=1;req.tag=2;req.direction=ANS_DMA_FROM_DEVICE;req.transfer_length=4096;
 memcpy(req.sqe,&c,64);command(&s,ANS_OP_AUTHORIZE_SUBMIT);assert(resp.result==0 && f.last.prp1==0x100004000ULL);
 command(&s,ANS_OP_AUTHORIZE_SUBMIT);assert(resp.result==ANS_RESULT_BAD_STATE);
 completion(&s,&f,2);
 memset(req.sqe,0,64);req.direction=0;req.transfer_length=0;
 command(&s,ANS_OP_REAP_COMPLETION);assert(resp.result==0 && !s.pending);
 /* Native CQ/SQ creation are admin traffic, never disk WRITE. */
 assert(s.telemetry.starts==1 && s.telemetry.submitted==3 && s.telemetry.completed==3);
 assert(s.telemetry.reads==1 && s.telemetry.writes==0 && s.telemetry.bytes_read==4096);
 assert(s.telemetry.current_outstanding==0 && s.telemetry.max_outstanding==1);
 struct ans_service saved=s;struct fake saved_f=f;struct ans_broker_request saved_req=req;
 /* No-data FLUSH is a real I/O submission, never a data WRITE. */
 struct nvme_command flush_c={.opcode=0,.tag=2,.nsid=1};
 memcpy(req.sqe,&flush_c,64);command(&s,ANS_OP_AUTHORIZE_SUBMIT);assert(resp.result==0);
 completion(&s,&f,2);
 assert(s.telemetry.flushes==1 && s.telemetry.writes==0 && s.telemetry.bytes_written==0);
 s=saved;f=saved_f;req=saved_req;
 /* Failed READ CQE completes the request but transfers no successful bytes. */
 memcpy(req.sqe,&c,64);req.direction=ANS_DMA_FROM_DEVICE;req.transfer_length=4096;
 command(&s,ANS_OP_AUTHORIZE_SUBMIT);assert(resp.result==0);
 f.cqe=(struct nvme_completion){.tag=2,.status=3};f.next=1;
 assert(ans_service_drain(&s)==0 && s.telemetry.bytes_read==4096 && s.telemetry.completed==4);
 s=saved;f=saved_f;req=saved_req;
 /* A frozen metadata snapshot survives subsequent counter mutation. */
 struct ans_telemetry snapshot;ans_service_telemetry_snapshot(&s,&snapshot);
 s.telemetry.submitted=UINT64_MAX;
 memcpy(req.sqe,&c,64);req.direction=ANS_DMA_FROM_DEVICE;req.transfer_length=4096;
 command(&s,ANS_OP_AUTHORIZE_SUBMIT);assert(resp.result==0);
 assert(!(s.telemetry.valid_fields&ANS_TELEM_SUBMITTED) && snapshot.submitted==3);
 s=saved;f=saved_f;req=saved_req;
 /* Uncertain publication cannot be claimed as a measured zero or success. */
 f.fail_submit=true;memcpy(req.sqe,&c,64);req.direction=ANS_DMA_FROM_DEVICE;req.transfer_length=4096;
 command(&s,ANS_OP_AUTHORIZE_SUBMIT);assert(resp.result==ANS_RESULT_IO_ERROR);
 assert(!(s.telemetry.valid_fields&(ANS_TELEM_SUBMITTED|ANS_TELEM_CURRENT_OUTSTANDING|ANS_TELEM_BYTES_READ)));
 assert(s.telemetry.valid_fields&ANS_TELEM_CQES);
 s=saved;f=saved_f;req=saved_req;
 /* A real PRP authorization rejection counts at the translation boundary. */
 c.prp1=4;memcpy(req.sqe,&c,64);req.direction=ANS_DMA_FROM_DEVICE;req.transfer_length=4096;
 command(&s,ANS_OP_AUTHORIZE_SUBMIT);assert(resp.result!=0 && s.telemetry.prp_translation_failures==1);
 s=saved;f=saved_f;req=saved_req;c.prp1=0x4000;
 c.opcode=1;memcpy(req.sqe,&c,64);req.direction=ANS_DMA_TO_DEVICE;req.transfer_length=4096;
 command(&s,ANS_OP_AUTHORIZE_SUBMIT);assert(resp.result==ANS_RESULT_DENIED);
 memset(req.sqe,0,64);req.tag=0;req.queue=0;req.direction=0;req.transfer_length=0;
 command(&s,ANS_OP_QUIESCE);assert(resp.result==0);
 f.fail_stop=true;command(&s,ANS_OP_RESET);assert(resp.result==ANS_RESULT_IO_ERROR && resp.generation==1);
 f.fail_stop=false;command(&s,ANS_OP_RESET);assert(resp.result==0 && resp.generation==2 && resp.state==ANS_STATE_READY);
 assert(s.telemetry.resets==2 && s.telemetry.stops==2 && s.telemetry.current_outstanding==0);
 puts("ANS service ownership/admin/translation/completion/telemetry tests PASS");
}
