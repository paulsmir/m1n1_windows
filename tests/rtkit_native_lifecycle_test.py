#!/usr/bin/env python3
"""Exercise actual RTKit lifecycle/dispatcher bodies against a finite mailbox model."""
import os
import re
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/rtkit.c').read_text()
header = (ROOT / 'src/rtkit.h').read_text()

def function(name):
    # Functions are file-scope and closing braces are in column zero.
    match = re.search(r'^(?:static )?(?:bool|void|int) ' + re.escape(name) + r'\(', source, re.M)
    assert match, name
    at = start = match.start()
    end = source.index('\n}', at) + 2
    return source[start:end]

macros = source[source.index('#define RTKIT_EP_MGMT'):source.index('\nstruct rtkit_dev {')]
stages = header[header.index('enum rtkit_stop_stage {'):header.index('\nbool rtkit_boot_bounded')]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef uint64_t u64; typedef uint32_t u32; typedef uint8_t u8;
#define BIT(n) (UINT64_C(1) << (n))
#define GENMASK(h,l) ((UINT64_MAX >> (63-(h))) & (UINT64_MAX << (l)))
#define FIELD_GET(mask,value) (((value)&(mask)) >> __builtin_ctzll(mask))
#define FIELD_PREP(mask,value) (((u64)(value) << __builtin_ctzll(mask)) & (mask))
#define USEC_PER_SEC 1000000
#define SZ_16K 16384
#define ALIGN_UP(n,a) (((n)+(a)-1)&~((a)-1))
#define min(a,b) ((a)<(b)?(a):(b))
#define rtkit_printf(...) ((void)0)
struct asc_message {u64 msg0;u32 msg1;};
struct rtkit_message {u8 ep;u64 msg;};
struct rtkit_deferred_message {u8 ep;u64 msg;};
struct rtkit_buffer {void *bfr;u64 dva;size_t sz;};
''' + macros + stages + r'''
struct rtkit_dev {
 bool bounded, strict_shutdown, crashed; u64 deadline;
 int *asc; int deferred, endpoints;
 bool sram;void *dart;u64 dva_base;char *name;
 enum rtkit_power_state ap_power,iop_power;
 struct rtkit_buffer syslog_bfr,crashlog_bfr,ioreport_bfr;
 u32 syslog_cnt,syslog_size;
};
typedef struct rtkit_dev rtkit_dev_t;
static struct asc_message messages[8192];
static unsigned head,tail,sends,allocations,stops,old_sends,old_receives;
static unsigned unmaps,buffer_frees;static void *allocated_buffer;
static u64 now;
static bool running,send_stuck,ack_missing,cpu_stuck,flood,booting,epmap_storm,skip_iop,block_second;
static bool timeout_expired(u64 deadline) {return ++now > deadline;}
static void udelay(u32 usec) {now+=usec;}
static void push(u32 type,u32 state) {
 assert(tail<8192);
 messages[tail++]=(struct asc_message){FIELD_PREP(MGMT_TYPE,type)|state,0};
}
static void asc_cpu_start(int *asc) {(void)asc;running=true;}
static void asc_cpu_stop(int *asc) {(void)asc;stops++;if(!cpu_stuck)running=false;}
static bool asc_cpu_running(int *asc) {(void)asc;return running;}
static bool asc_try_send(int *asc,const struct asc_message *msg) {
 (void)asc;if(send_stuck||(block_second&&sends==1))return false;sends++;
 unsigned type=FIELD_GET(MGMT_TYPE,msg->msg0),state=FIELD_GET(MGMT_PWR_STATE,msg->msg0);
 if (msg->msg1!=0 || ack_missing || (skip_iop&&type==MGMT_MSG_IOP_PWR_STATE))return true;
 if (booting && type==MGMT_MSG_IOP_PWR_STATE && state==RTKIT_POWER_INIT)
   push(MGMT_MSG_HELLO,11 | (12u<<16));
 else if(booting && type==MGMT_MSG_HELLO_ACK) {
   push(MGMT_MSG_EPMAP,0);if(!epmap_storm) messages[tail-1].msg0 |= MGMT_MSG_EPMAP_DONE;
 } else if(booting && type==MGMT_MSG_EPMAP_REPLY) {
   if(epmap_storm)push(MGMT_MSG_EPMAP,0);else push(MGMT_MSG_IOP_PWR_STATE_ACK,RTKIT_POWER_ON);
 } else if(type==MGMT_MSG_AP_PWR_STATE)push(MGMT_MSG_AP_PWR_STATE_ACK,state);
 else if(type==MGMT_MSG_IOP_PWR_STATE)push(MGMT_MSG_IOP_PWR_STATE_ACK,state);
 return true;
}
static bool asc_send(int *asc,const struct asc_message *msg) {old_sends++;return asc_try_send(asc,msg);}
static bool asc_recv(int *asc,struct asc_message *msg) {
 (void)asc;if(head<tail){*msg=messages[head++];return true;}
 if(flood){*msg=(struct asc_message){FIELD_PREP(MGMT_TYPE,MSG_SYSLOG_LOG),RTKIT_EP_SYSLOG};return true;}
 return false;
}
static bool asc_recv_timeout(int *asc,struct asc_message *msg,u32 usec) {(void)usec;old_receives++;return asc_recv(asc,msg);}
static bool rtkit_deferred_pop(int *q,struct rtkit_deferred_message *msg) {(void)q;(void)msg;return false;}
static bool rtkit_endpoint_map_contains(int *q,u8 ep) {(void)q;(void)ep;return true;}
static bool rtkit_endpoint_map_add_chunk(int *q,u32 base,u32 bitmap) {(void)q;(void)bitmap;return base<8;}
static void *memalign(size_t alignment,size_t size) {
 (void)alignment;assert(!allocated_buffer);allocations++;
 allocated_buffer=calloc(1,size);assert(allocated_buffer);return allocated_buffer;
}
static bool is_heap(void *p) {return p==allocated_buffer;}
static bool rtkit_map(rtkit_dev_t *r,void *p,size_t size,u64 *dva) {
 (void)r;(void)p;(void)size;*dva=0x100000;return true;
}
static bool rtkit_unmap(rtkit_dev_t *r,u64 dva,size_t size) {
 (void)r;assert(dva==0x100000&&size==16384);unmaps++;return true;
}
static void *dart_translate(void *dart,u64 dva) {(void)dart;(void)dva;return NULL;}
static void tracked_free(void *p) {
 if(p&&p==allocated_buffer){allocated_buffer=NULL;buffer_frees++;}free(p);
}
#define free tracked_free
static void rtkit_crashed(rtkit_dev_t *rtk) {rtk->crashed=true;}
'''
harness += function('rtkit_asc_send') + '\n' + function('rtkit_asc_recv_timeout')
for name in ['rtkit_alloc_buffer','rtkit_free_buffer','rtkit_handle_buffer_request','rtkit_free']:
    harness += '\n' + function(name)
harness += r'''
static bool rtkit_send(rtkit_dev_t *rtk,const struct rtkit_message *msg) {
 struct asc_message asc={msg->msg,msg->ep};return rtkit_asc_send(rtk,&asc);
}
'''
harness += source[source.index('enum rtkit_recv_one_result {'):source.index('\nstatic int rtkit_recv_one_dispatch')]
for name in ['rtkit_recv_one_dispatch', 'rtkit_recv', 'rtkit_start_ep', 'rtkit_boot',
             'rtkit_boot_bounded', 'rtkit_poll_system_once', 'rtkit_poll_boot_once', 'rtkit_sleep_bounded']:
    harness += '\n' + function(name)
harness += r'''
static rtkit_dev_t reset(void) {
 assert(!allocated_buffer);unmaps=buffer_frees=0;
 head=tail=sends=allocations=stops=old_sends=old_receives=0;now=0;
 send_stuck=ack_missing=cpu_stuck=flood=booting=epmap_storm=skip_iop=block_second=false;running=true;
 return (rtkit_dev_t){.ap_power=RTKIT_POWER_ON,.iop_power=RTKIT_POWER_ON};
}
int main(void) {
 u32 stage;rtkit_dev_t r=reset();
 assert(rtkit_sleep_bounded(&r,1000,&stage));
 assert(stage==RTKIT_STOP_DONE&&sends==2&&stops==1&&!running&&!r.bounded);
 assert(r.ap_power==RTKIT_POWER_QUIESCED&&r.iop_power==RTKIT_POWER_SLEEP);
 r=reset();r.ap_power=RTKIT_POWER_QUIESCED;r.iop_power=RTKIT_POWER_SLEEP;ack_missing=true;
 assert(!rtkit_sleep_bounded(&r,30,&stage)); /* cached state cannot satisfy new ACK */
 assert(stage==RTKIT_STOP_AP_ACK&&stops==0&&running&&now<=32);
 r=reset();send_stuck=true;
 assert(!rtkit_sleep_bounded(&r,30,&stage)&&stage==RTKIT_STOP_AP_SEND&&stops==0);
 r=reset();ack_missing=true;flood=true;
 assert(!rtkit_sleep_bounded(&r,50,&stage)&&stage==RTKIT_STOP_AP_ACK&&now<=53);
 assert(sends>2&&allocations==0&&stops==0); /* endless system traffic is bounded */
 r=reset();skip_iop=true;
 assert(!rtkit_sleep_bounded(&r,50,&stage)&&stage==RTKIT_STOP_IOP_ACK&&stops==0);
 r=reset();block_second=true;
 assert(!rtkit_sleep_bounded(&r,50,&stage)&&stage==RTKIT_STOP_IOP_SEND&&stops==0);
 r=reset();cpu_stuck=true;
 assert(!rtkit_sleep_bounded(&r,50,&stage)&&stage==RTKIT_STOP_CPU&&running);
 r=reset();messages[tail++]=(struct asc_message){FIELD_PREP(MGMT_TYPE,MSG_BUFFER_REQUEST),RTKIT_EP_SYSLOG};
 assert(!rtkit_sleep_bounded(&r,1000,&stage)&&allocations==0&&stops==0);
 r=reset();r.crashed=true;assert(!rtkit_sleep_bounded(&r,1000,&stage)&&sends==0);
 r=reset();booting=true;
 assert(rtkit_boot_bounded(&r,1000)&&r.ap_power==RTKIT_POWER_ON&&r.iop_power==RTKIT_POWER_ON);
 assert(!old_sends&&!old_receives&&!r.bounded);
 r=reset();booting=true;epmap_storm=true;
 assert(!rtkit_boot_bounded(&r,100)&&now<=103&&stops==0&&running);
 r=reset();struct asc_message msg={0};
 assert(rtkit_asc_send(&r,&msg)&&old_sends==1);
 (void)rtkit_asc_recv_timeout(&r,&msg,100);assert(old_receives==1);
 /* Actual allocation/map/handler/failed reply: retain until stopped retire. */
 r=reset();r.bounded=true;r.deadline=30;send_stuck=true;
 struct rtkit_message request={.ep=RTKIT_EP_SYSLOG,
   .msg=FIELD_PREP(MGMT_TYPE,MSG_BUFFER_REQUEST)|FIELD_PREP(MSG_BUFFER_REQUEST_SIZE,4)};
 assert(!rtkit_handle_buffer_request(&r,&request,&r.syslog_bfr));
 assert(allocations==1&&allocated_buffer==r.syslog_bfr.bfr&&unmaps==0&&buffer_frees==0);
 assert(!rtkit_handle_buffer_request(&r,&request,&r.syslog_bfr));
 assert(allocations==1&&unmaps==0); /* repeated request cannot lose ownership */
 r.bounded=false;send_stuck=false;
 assert(rtkit_sleep_bounded(&r,now+1000,&stage));
 rtkit_dev_t *owner=malloc(sizeof(*owner));assert(owner);*owner=r;owner->name=malloc(4);
 rtkit_free(owner);assert(!allocated_buffer&&unmaps==1&&buffer_frees==1);
 /* Legacy failed reply keeps its existing unmap/free behavior. */
 r=reset();send_stuck=true;
 assert(!rtkit_handle_buffer_request(&r,&request,&r.syslog_bfr));
 assert(allocations==1&&unmaps==1&&buffer_frees==1&&!allocated_buffer);
 /* Identical late buffer request is serviced during init, rejected during stop. */
 r=reset();messages[tail++]=(struct asc_message){request.msg,request.ep};
 assert(rtkit_poll_boot_once(&r,1000)==RTKIT_RECV_ONE_SYSTEM);
 assert(allocations==1&&r.syslog_bfr.bfr==allocated_buffer&&!r.bounded);
 assert(rtkit_sleep_bounded(&r,1000,&stage));
 owner=malloc(sizeof(*owner));assert(owner);*owner=r;owner->name=malloc(4);
 rtkit_free(owner);assert(unmaps==1&&buffer_frees==1);
 r=reset();messages[tail++]=(struct asc_message){request.msg,request.ep};
 assert(rtkit_poll_system_once(&r,1000)==RTKIT_RECV_ONE_ERROR);
 assert(!allocations&&!allocated_buffer);
 puts("RTKit bounded boot/stop actual-function tests PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='rtkit-native-lifecycle-') as d:
    c = Path(d)/'test.c'; exe=Path(d)/'test'; c.write_text(harness)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-g',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
