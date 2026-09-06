/* SPDX-License-Identifier: MIT */
#include "hv_ans_pins.h"
#include <string.h>
static bool span(uint64_t ipa,uint64_t bytes,uint64_t *base,uint64_t *size)
{
    if(!bytes||ipa>UINT64_MAX-bytes||ipa+bytes>UINT64_MAX-0x3fff)return false;
    *base=ipa&~UINT64_C(0x3fff);
    *size=((ipa+bytes+0x3fff)&~UINT64_C(0x3fff))-*base;
    return true;
}
bool ans_pin_add(struct ans_pin_set*s,unsigned tag,uint64_t ipa,uint64_t bytes)
{
    uint64_t base,size;
    if(!s||tag>=ANS_QUEUE_DEPTH||s->counts[tag]>=ANS_MAX_PIN_RANGES||!span(ipa,bytes,&base,&size))return false;
    s->ranges[tag][s->counts[tag]++]=(struct ans_pin_range){base,size};
    return true;
}
bool ans_pin_mapping_allowed(const struct ans_pin_set*s,uint64_t ipa,uint64_t bytes)
{
    uint64_t base,size;
    if(!s)return false;
    if(!bytes)return true;
    if(!span(ipa,bytes,&base,&size))return false;
    for(unsigned t=0;t<ANS_QUEUE_DEPTH;t++) {
        if(s->counts[t]>ANS_MAX_PIN_RANGES)return false;
        for(unsigned i=0;i<s->counts[t];i++){
            const struct ans_pin_range*r=&s->ranges[t][i];
            if(r->ipa>UINT64_MAX-r->size || (r->size && base<r->ipa+r->size && r->ipa<base+size))return false;
        }
    }
    return true;
}
void ans_pin_release(struct ans_pin_set*s,unsigned tag)
{
    if(!s||tag>=ANS_QUEUE_DEPTH)return;
    memset(s->ranges[tag],0,sizeof(s->ranges[tag]));s->counts[tag]=0;
}
