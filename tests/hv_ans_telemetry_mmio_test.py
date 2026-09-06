#!/usr/bin/env python3
"""Compile the production broker MMIO dispatcher with inert hardware boundaries."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/hv_ans_platform.c').read_text()
body = source[source.index('static bool inside('):source.index('static bool private_mmio(')]
harness = r'''
#include "hv_ans_service.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;
#define UNUSED(x) (void)(x)
struct exc_info { int unused; };
static struct {
    struct ans_service service;
    struct ans_broker_request request;
    struct ans_broker_response response;
    bool mapped;
    unsigned active_tag;
    u64 physical_irqs, pending_snapshot;
    struct ans_telemetry telemetry_snapshot;
    u32 request_words;
} runtime;
static unsigned getter_calls;
static u64 nvme_native_tcb_inval_failures(void) { getter_calls++;return 7; }
static void hv_quarantine_guest(void) { abort(); }
'''
test = r'''
int main(void) {
    runtime.mapped=true;
    runtime.service.telemetry=(struct ans_telemetry){.magic=ANS_TELEMETRY_MAGIC,
        .size=sizeof(struct ans_telemetry),.source=2,.valid_fields=ANS_TELEM_SUBMITTED,.submitted=10};
    runtime.service.broker.header.generation=4;runtime.physical_irqs=12;
    u64 value=0;
    assert(broker_mmio(NULL,ANS_BROKER_BASE+ANS_TELEMETRY_OFFSET,&value,false,2));
    assert(value==ANS_TELEMETRY_MAGIC && getter_calls==1);
    runtime.service.telemetry.submitted=99;runtime.physical_irqs=44;
    struct ans_telemetry snapshot={0};snapshot.magic=(uint32_t)value;
    for(unsigned off=4;off<sizeof(snapshot);off+=4) {
        value=0;assert(broker_mmio(NULL,ANS_BROKER_BASE+ANS_TELEMETRY_OFFSET+off,&value,false,2));
        memcpy((u8*)&snapshot+off,&value,4);
    }
    assert(snapshot.submitted==10 && snapshot.physical_irqs==12 && snapshot.tcb_inval_failures==7);
    assert(getter_calls==1 && snapshot.generation==4);
    value=123;broker_mmio(NULL,ANS_BROKER_BASE+ANS_TELEMETRY_OFFSET,&value,true,2);
    assert(getter_calls==1 && runtime.telemetry_snapshot.magic==ANS_TELEMETRY_MAGIC);
    runtime.physical_irqs=UINT64_MAX;
    broker_mmio(NULL,ANS_BROKER_BASE+ANS_TELEMETRY_OFFSET,&value,false,2);
    assert(!(runtime.telemetry_snapshot.valid_fields&ANS_TELEM_PHYSICAL_IRQS));
    assert(runtime.telemetry_snapshot.submitted==99 && getter_calls==2);
    runtime.service.telemetry.valid_fields|=ANS_TELEM_BROKER_REJECTIONS;
    runtime.service.broker.owner=ANS_OWNER_NATIVE;
    runtime.service.broker.header.state=ANS_STATE_CLAIMED;
    runtime.service.broker.header.sequence=23;
    runtime.service.broker.tags[2].state=ANS_TAG_STATE_IN_FLIGHT;
    runtime.request=(struct ans_broker_request){.sequence=24,.queue=1,.tag=2};
    runtime.request_words=1; /* Incomplete 28-DWORD frame. */
    value=24;broker_mmio(NULL,ANS_BROKER_BASE+ANS_DOORBELL_OFFSET,&value,true,2);
    assert(runtime.response.result==ANS_RESULT_BAD_SEQUENCE && runtime.response.sequence==24);
    assert(runtime.response.queue==1 && runtime.response.tag==2 && runtime.response.generation==4);
    assert(runtime.service.telemetry.broker_rejections==1 && runtime.service.rejected==1);
    assert(runtime.service.broker.header.last_error==ANS_RESULT_BAD_SEQUENCE);
    /* Complete frame with a mismatched doorbell is also one rejection. */
    runtime.request_words=(1u<<(sizeof(runtime.request)/4))-1u;
    value=25;broker_mmio(NULL,ANS_BROKER_BASE+ANS_DOORBELL_OFFSET,&value,true,2);
    assert(runtime.response.result==ANS_RESULT_BAD_SEQUENCE && runtime.response.sequence==24);
    assert(runtime.service.telemetry.broker_rejections==2 && runtime.service.rejected==2);
    assert(runtime.service.broker.header.sequence==23 && runtime.service.broker.header.generation==4);
    assert(runtime.service.broker.owner==ANS_OWNER_NATIVE && runtime.service.broker.header.state==ANS_STATE_CLAIMED);
    assert(runtime.service.broker.tags[2].state==ANS_TAG_STATE_IN_FLIGHT);
    broker_mmio(NULL,ANS_BROKER_BASE+ANS_TELEMETRY_OFFSET,&value,false,2);
    assert(runtime.telemetry_snapshot.broker_rejections==2 && runtime.telemetry_snapshot.last_error==ANS_RESULT_BAD_SEQUENCE);
    runtime.service.telemetry.broker_rejections=UINT64_MAX;
    value=25;broker_mmio(NULL,ANS_BROKER_BASE+ANS_DOORBELL_OFFSET,&value,true,2);
    broker_mmio(NULL,ANS_BROKER_BASE+ANS_TELEMETRY_OFFSET,&value,false,2);
    assert(!(runtime.telemetry_snapshot.valid_fields&ANS_TELEM_BROKER_REJECTIONS));
    assert(runtime.telemetry_snapshot.broker_rejections==UINT64_MAX);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='ans-mmio-') as d:
    c = Path(d)/'test.c'; c.write_text(harness+body+test)
    binary = Path(d)/'test'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I',str(root/'src'),str(c),str(root/'src/hv_ans_service.c'),
                    str(root/'src/hv_ans_broker.c'),str(root/'src/hv_ans_prp.c'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('ANS production telemetry MMIO immutable snapshot tests PASS')
