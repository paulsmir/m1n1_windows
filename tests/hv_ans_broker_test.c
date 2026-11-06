/* Deterministic invariants: exclusivity, generation and exact tag lifetime. */
#include "hv_ans_broker.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void ready(struct ans_broker *b) {
    ans_broker_init(b);
    assert(ans_broker_claim(b)==ANS_RESULT_BAD_STATE);
    assert(ans_broker_handoff(b,true,false,true,true)==ANS_RESULT_IO_ERROR);
    assert(b->owner==ANS_OWNER_RECOVERY);
    assert(ans_broker_handoff(b,true,true,true,true)==ANS_RESULT_OK);
    assert(ans_broker_claim(b)==0);
    assert(ans_broker_claim(b)==ANS_RESULT_BAD_STATE);
    assert(ans_broker_register_queues(b)==0);
}
int main(void) {
    struct ans_broker b; uint8_t cqe[16]={0}, out[16];
    ready(&b);
    uint64_t gen=b.header.generation;
    assert(ans_broker_prepare(&b,0,2)==ANS_RESULT_BAD_TAG);
    assert(ans_broker_prepare(&b,1,0)==ANS_RESULT_BAD_TAG);
    assert(ans_broker_prepare(&b,2,1)==ANS_RESULT_BAD_QUEUE);
    assert(ans_broker_prepare(&b,1,64)==ANS_RESULT_BAD_TAG);
    for (unsigned t=ANS_ADMIN_TAGS;t<64;t++) {
        assert(ans_broker_prepare(&b,1,t)==0);
        assert(ans_broker_submit(&b,1,t)==0);
        assert(ans_broker_submit(&b,1,t)==ANS_RESULT_BAD_STATE);
        assert(ans_broker_prepare(&b,1,t)==ANS_RESULT_BAD_STATE);
        assert(ans_broker_reap(&b,1,t,out)==ANS_RESULT_BAD_STATE);
    }
    assert(ans_broker_prepare(&b,0,1)==0);
    assert(ans_broker_submit(&b,0,1)==0);
    assert(ans_broker_prepare(&b,0,0)==0);
    assert(ans_broker_submit(&b,0,0)==0);
    assert(ans_broker_complete(&b,gen-1,1,2,cqe)==ANS_RESULT_STALE);
    for (unsigned t=0;t<64;t++) {
        cqe[12]=(uint8_t)t;
        assert(ans_broker_complete(&b,gen,t>=ANS_ADMIN_TAGS?1:0,t,cqe)==0);
        assert(ans_broker_complete(&b,gen,t>=ANS_ADMIN_TAGS?1:0,t,cqe)==ANS_RESULT_BAD_STATE);
        assert(ans_broker_reap(&b,t>=ANS_ADMIN_TAGS?1:0,t,out)==0);
        assert(out[12]==t);
    }
    struct ans_broker_request r={.abi_major=ANS_ABI_MAJOR,.size=sizeof(r),
        .generation=gen,.sequence=1,.operation=ANS_OP_QUERY};
    assert(ans_broker_request_begin(&b,&r)==0);
    assert(ans_broker_request_begin(&b,&r)==ANS_RESULT_BAD_SEQUENCE);
    r.sequence=2;r.abi_major++;
    assert(ans_broker_request_begin(&b,&r)==ANS_RESULT_BAD_ABI);
    r.abi_major--;r.generation--;
    assert(ans_broker_request_begin(&b,&r)==ANS_RESULT_STALE);
    assert(ans_broker_prepare(&b,1,2)==0);
    assert(ans_broker_submit(&b,1,2)==0);
    assert(ans_broker_quiesce(&b)==0);
    assert(ans_broker_reset(&b,false)==ANS_RESULT_IO_ERROR);
    assert(b.tags[2].state==ANS_TAG_STATE_IN_FLIGHT);
    assert(ans_broker_reset(&b,true)==0);
    assert(b.header.generation==gen+1);
    assert(!b.queues_registered);
    assert(ans_broker_complete(&b,gen,1,2,cqe)==ANS_RESULT_STALE);
    assert(ans_broker_claim(&b)==0);
    assert(ans_broker_register_queues(&b)==0);
    assert(ans_broker_prepare(&b,1,2)==0);
    assert(ans_broker_submit(&b,1,2)==0);
    assert(ans_broker_release(&b,false)==ANS_RESULT_IO_ERROR);
    assert(ans_broker_quiesce(&b)==0);
    assert(ans_broker_release(&b,true)==0);
    assert(b.owner==ANS_OWNER_NONE);
    assert(ans_broker_claim(&b)==0); /* second stopped lifetime */
    uint8_t sqe[64]={2}; sqe[4]=1; sqe[2]=2;
    const struct ans_write_window none={0,0}, win={10,8}, wrap={UINT64_MAX-1,4}, past={96,8};
    assert(ans_broker_validate_io(sqe,2,4096,ANS_DMA_FROM_DEVICE,100,NULL)==0);
    sqe[2]=0; assert(ans_broker_validate_io(sqe,2,4096,1,100,NULL)==ANS_RESULT_BAD_TAG);
    sqe[2]=63; assert(ans_broker_validate_io(sqe,2,4096,1,100,NULL)==ANS_RESULT_BAD_TAG);
    assert(ans_broker_validate_io(sqe,63,4096,1,100,NULL)==0);
    sqe[2]=64; assert(ans_broker_validate_io(sqe,64,4096,1,100,NULL)==ANS_RESULT_BAD_TAG);
    sqe[2]=255; assert(ans_broker_validate_io(sqe,255,4096,1,100,NULL)==ANS_RESULT_BAD_TAG);
    sqe[2]=2;
    /* READ is never window-restricted. */
    sqe[40]=50; assert(ans_broker_validate_io(sqe,2,4096,ANS_DMA_FROM_DEVICE,100,&win)==0);
    sqe[40]=0;
    sqe[0]=0; assert(ans_broker_validate_io(sqe,2,0,ANS_DMA_NONE,100,NULL)==0);
    assert(ans_broker_validate_io(sqe,2,0,ANS_DMA_NONE,100,&win)==0);
    sqe[0]=1;
    /* No window (NULL or empty): WRITE is denied before any doorbell. */
    sqe[40]=10;
    assert(ans_broker_validate_io(sqe,2,4096,ANS_DMA_TO_DEVICE,100,NULL)==ANS_RESULT_DENIED);
    assert(ans_broker_validate_io(sqe,2,4096,ANS_DMA_TO_DEVICE,100,&none)==ANS_RESULT_DENIED);
    /* Inside the window: first, last, and a full-window transfer. */
    assert(ans_broker_validate_io(sqe,2,4096,ANS_DMA_TO_DEVICE,100,&win)==0);
    sqe[40]=17; assert(ans_broker_validate_io(sqe,2,4096,ANS_DMA_TO_DEVICE,100,&win)==0);
    sqe[40]=10; sqe[48]=7; assert(ans_broker_validate_io(sqe,2,8*4096,ANS_DMA_TO_DEVICE,100,&win)==0);
    /* Straddling either edge or fully outside: DENIED. */
    sqe[40]=17; sqe[48]=1; assert(ans_broker_validate_io(sqe,2,8192,ANS_DMA_TO_DEVICE,100,&win)==ANS_RESULT_DENIED);
    sqe[40]=9; assert(ans_broker_validate_io(sqe,2,8192,ANS_DMA_TO_DEVICE,100,&win)==ANS_RESULT_DENIED);
    sqe[48]=0;
    sqe[40]=9; assert(ans_broker_validate_io(sqe,2,4096,ANS_DMA_TO_DEVICE,100,&win)==ANS_RESULT_DENIED);
    sqe[40]=18; assert(ans_broker_validate_io(sqe,2,4096,ANS_DMA_TO_DEVICE,100,&win)==ANS_RESULT_DENIED);
    sqe[40]=0; assert(ans_broker_validate_io(sqe,2,4096,ANS_DMA_TO_DEVICE,100,&win)==ANS_RESULT_DENIED);
    /* A wrapping window or one past the namespace is invalid and denies everything. */
    memset(sqe+40,0xff,8); sqe[40]=0xfe;
    assert(ans_broker_validate_io(sqe,2,4096,ANS_DMA_TO_DEVICE,UINT64_MAX,&wrap)==ANS_RESULT_DENIED);
    memset(sqe+40,0,8); sqe[40]=96;
    assert(ans_broker_validate_io(sqe,2,4096,ANS_DMA_TO_DEVICE,100,&past)==ANS_RESULT_DENIED);
    /* Format errors still win over the window decision. */
    sqe[40]=10;
    assert(ans_broker_validate_io(sqe,2,4096,ANS_DMA_FROM_DEVICE,100,&win)==ANS_RESULT_BAD_COMMAND);
    assert(ans_broker_validate_io(sqe,2,0,ANS_DMA_TO_DEVICE,100,&win)==ANS_RESULT_BAD_COMMAND);
    sqe[40]=100;
    assert(ans_broker_validate_io(sqe,2,4096,ANS_DMA_TO_DEVICE,100,&win)==ANS_RESULT_BAD_COMMAND);
    puts("ANS ownership/tag/sequence/write-window tests PASS");
}
