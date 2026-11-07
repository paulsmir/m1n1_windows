#include "hv_ans_prp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct memory { uint64_t deny; uint32_t dir; uint64_t list[32]; unsigned reads; };
static bool translate(void *ctx,uint64_t ipa,size_t n,uint32_t d,uint64_t *pa) {
    struct memory *m=ctx;
    if (ipa<0x1000 || ipa>UINT64_MAX-n || (ipa<=m->deny && m->deny<ipa+n) ||
        (ipa>=0x900000000ULL && ipa<0xa00000000ULL)) return false;
    if (ipa!=0x80000 && d!=m->dir) return false;
    *pa=ipa+0x100000000ULL; return true;
}
static bool readmeta(void *ctx,uint64_t pa,void *dst,size_t n) {
    struct memory *m=ctx;
    if (pa!=0x100080000ULL || n>sizeof(m->list)) return false;
    memcpy(dst,m->list,n); m->reads++;return true;
}
int main(void) {
    struct memory m={.deny=UINT64_MAX,.dir=ANS_DMA_FROM_DEVICE};
    struct ans_memory_ops ops={translate,readmeta}; struct ans_prp_shadow s;
    assert(ans_prp_translate(&ops,&m,0x1000,0,4096,1,&s)==0);
    assert(s.page_count==1 && s.host_pages[0]==0x100001000ULL && !m.reads);
    assert(ans_prp_translate(&ops,&m,0x1008,0,8,1,&s)==0);
    assert(s.host_pages[0]==0x100001008ULL);
    assert(ans_prp_translate(&ops,&m,0x1008,0x5000,4096,1,&s)==0 && s.page_count==2);
    assert(ans_prp_translate(&ops,&m,0x1000,0x5000,8192,1,&s)==0);
    assert(ans_prp_translate(&ops,&m,0xfffff000ULL,0x100000000ULL,8192,1,&s)==0);
    for (unsigned i=0;i<32;i++) m.list[i]=0x10000+i*0x2000;
    assert(ans_prp_translate(&ops,&m,0x1000,0x80000,131072,1,&s)==0 && s.page_count==32);
    assert(ans_prp_translate(&ops,&m,0x1008,0x80000,131072,1,&s)==0 && s.page_count==33);
    assert(m.reads==2);
    m.deny=0x11000-1;
    assert(ans_prp_translate(&ops,&m,0x1000,0x80000,131072,1,&s)==ANS_RESULT_DENIED);
    m.deny=UINT64_MAX;
    assert(ans_prp_translate(&ops,&m,0,0,4096,1,&s)!=0);
    assert(ans_prp_translate(&ops,&m,0x900001000ULL,0,4096,1,&s)==ANS_RESULT_DENIED);
    assert(ans_prp_translate(&ops,&m,0x1000,0,0,1,&s)!=0);
    assert(ans_prp_translate(&ops,&m,0x1000,0x80000,131073,1,&s)!=0);
    assert(ans_prp_translate(&ops,&m,0x1000,0x80008,131072,1,&s)!=0);
    m.list[10]=0x80000;
    assert(ans_prp_translate(&ops,&m,0x1000,0x80000,131072,1,&s)==ANS_RESULT_BAD_PRP);
    m.list[10]=0x31001;
    assert(ans_prp_translate(&ops,&m,0x1000,0x80000,131072,1,&s)==ANS_RESULT_BAD_PRP);
    m.list[10]=0x900001000ULL;
    assert(ans_prp_translate(&ops,&m,0x1000,0x80000,131072,1,&s)==ANS_RESULT_DENIED);
    m.dir=ANS_DMA_TO_DEVICE;
    assert(ans_prp_translate(&ops,&m,0x1000,0,4096,2,&s)==0);
    assert(ans_prp_translate(&ops,&m,0x1000,0,4096,1,&s)==ANS_RESULT_DENIED);
    assert(ans_prp_translate(&ops,&m,0x1000,0,4096,0,&s)!=0);
    puts("ANS PRP fragmentation/range/direction/metadata tests PASS");
}
