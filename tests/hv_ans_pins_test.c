#include "hv_ans_pins.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 struct ans_pin_set s={0};
 assert(ans_pin_add(&s,2,0x1008,4096));
 assert(!ans_pin_mapping_allowed(&s,0x1000,4)); /* 16KiB leaf also contains data. */
 assert(!ans_pin_mapping_allowed(&s,0x2000,4));
 assert(ans_pin_mapping_allowed(&s,0x4000,0x4000));
 assert(!ans_pin_add(&s,64,0x1000,4096));
 assert(!ans_pin_add(&s,3,UINT64_MAX-3,8));
 assert(!ans_pin_mapping_allowed(&s,UINT64_MAX-3,8));
 assert(ans_pin_add(&s,3,0x3ff8,16));
 assert(!ans_pin_mapping_allowed(&s,0x4000,4));
 ans_pin_release(&s,2);assert(!ans_pin_mapping_allowed(&s,0x1000,4));
 ans_pin_release(&s,3);assert(ans_pin_mapping_allowed(&s,0x1000,4));
 for(unsigned i=0;i<ANS_MAX_PIN_RANGES;i++)assert(ans_pin_add(&s,4,0x10000+(uint64_t)i*0x4000,4096));
 assert(!ans_pin_add(&s,4,0x800000,4096));
 assert(!ans_pin_mapping_allowed(&s,0,0x2000000));
 puts("ANS stage2 pin range tests PASS");
}
