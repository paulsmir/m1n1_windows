/* SPDX-License-Identifier: MIT */
#include "hv_ans_prp.h"
#include <string.h>

int ans_prp_translate(const struct ans_memory_ops *ops, void *ctx, uint64_t prp1,
                      uint64_t prp2, uint32_t length, uint32_t direction,
                      struct ans_prp_shadow *out)
{
    struct ans_prp_shadow tmp = {0};
    uint64_t guest[ANS_MAX_DATA_PAGES]={0}, list[ANS_MAX_DATA_PAGES-1]={0};
    if (!ops || !ops->translate || !ops->read_metadata || !out || !prp1 || (prp1&3) ||
        !length || length>ANS_MAX_TRANSFER ||
        (direction!=ANS_DMA_FROM_DEVICE && direction!=ANS_DMA_TO_DEVICE))
        return ANS_RESULT_BAD_PRP;
    uint32_t offset=(uint32_t)(prp1 & (ANS_PRP_PAGE_SIZE-1));
    uint32_t first=ANS_PRP_PAGE_SIZE-offset;
    if (first>length) first=length;
    uint32_t remaining=length-first;
    uint32_t extra=(remaining+ANS_PRP_PAGE_SIZE-1)/ANS_PRP_PAGE_SIZE;
    guest[0]=prp1;
    if (!remaining) {
        if (prp2) return ANS_RESULT_BAD_PRP;
    } else {
        if (!prp2 || (prp2 & (ANS_PRP_PAGE_SIZE-1))) return ANS_RESULT_BAD_PRP;
        if (extra==1) guest[1]=prp2;
        else {
            uint64_t list_pa;
            if (prp2==(prp1&~(uint64_t)(ANS_PRP_PAGE_SIZE-1))) return ANS_RESULT_BAD_PRP;
            if (!ops->translate(ctx,prp2,extra*sizeof(uint64_t),ANS_DMA_TO_DEVICE,&list_pa))
                return ANS_RESULT_DENIED;
            if ((list_pa&(ANS_PRP_PAGE_SIZE-1)) ||
                !ops->read_metadata(ctx,list_pa,list,extra*sizeof(uint64_t))) return ANS_RESULT_BAD_PRP;
            /* Snapshot once. Never reread a guest list after authorizing it. */
            for (uint32_t i=0;i<extra;i++) {
                if (!list[i] || (list[i]&(ANS_PRP_PAGE_SIZE-1)) || list[i]==prp2)
                    return ANS_RESULT_BAD_PRP;
                guest[i+1]=list[i];
            }
        }
    }
    uint32_t left=length;
    for (uint32_t i=0;i<=extra;i++) {
        uint64_t ipa=guest[i];
        uint32_t n=i==0?first:(left>ANS_PRP_PAGE_SIZE?ANS_PRP_PAGE_SIZE:left);
        if (ipa>UINT64_MAX-n) return ANS_RESULT_BAD_PRP;
        uint64_t pa;
        if (!ops->translate(ctx,ipa,n,direction,&pa)) return ANS_RESULT_DENIED;
        if (pa>UINT64_MAX-n || (pa&(ANS_PRP_PAGE_SIZE-1))!=(ipa&(ANS_PRP_PAGE_SIZE-1)))
            return ANS_RESULT_BAD_PRP;
        /* Conservative V1: reject aliasing pages rather than authorize overlapping DMA. */
        for (uint32_t j=0;j<i;j++)
            if ((pa&~(uint64_t)(ANS_PRP_PAGE_SIZE-1))==
                (tmp.host_pages[j]&~(uint64_t)(ANS_PRP_PAGE_SIZE-1))) return ANS_RESULT_BAD_PRP;
        tmp.host_pages[i]=pa;
        left-=n;
    }
    tmp.page_count=extra+1;
    tmp.length=length;
    *out=tmp;
    return ANS_RESULT_OK;
}
