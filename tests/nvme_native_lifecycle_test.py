#!/usr/bin/env python3
"""Actual NVMe lifecycle code with synthetic registers; no machine access."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'src/nvme.c').read_text()
def fn(name):
    at=s.index(name+'(');return s[s.rfind('\n',0,at)+1:s.index('\n}',at)+2]
constants=s[s.index('#define NVME_TIMEOUT'):s.index('\nstruct nvme_queue {')]
state=s[s.index('struct nvme_queue {'):s.index('\nstatic bool alloc_queue(')]
pre=r'''
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ans_queue.h"
typedef uint64_t u64;typedef uint32_t u32;typedef uint8_t u8;
typedef int asc_dev_t;typedef int rtkit_dev_t;typedef int sart_dev_t;
#define BIT(n) (UINT64_C(1) << (n))
#define GENMASK(h,l) ((UINT64_MAX >> (63-(h))) & (UINT64_MAX << (l)))
#define FIELD_GET(mask,value) (((value)&(mask)) >> __builtin_ctzll(mask))
#define FIELD_PREP(mask,value) (((u64)(value) << __builtin_ctzll(mask)) & (mask))
#define SZ_4K 4096
#define SZ_16K 16384
#define USEC_PER_SEC 1000000
#define PMGR_DIE_ID 3
#define ALIGNED(n) __attribute__((aligned(n)))
static unsigned prints;
#define printf(...) ((void)prints++)
#define static_assert _Static_assert
#define UNUSED(x) ((void)(x))
struct rtkit_message {u8 ep;u64 msg;};
'''
h=(ROOT/'src/nvme.h').read_text();stages=h[h.index('enum nvme_lifecycle_stage {'):h.index('\nbool nvme_native_init')]
rh=(ROOT/'src/rtkit.h').read_text();rstages=rh[rh.index('enum rtkit_stop_stage {'):rh.index('\nbool rtkit_boot_bounded')]
pre+=constants+stages+rstages+state+r'''
static unsigned live_allocations,starts,submits,system_polls,stop_calls,boot_polls,total_allocations,legacy_polls;
static bool boot_ok=true,fail_boot_status,fail_shutdown,fail_disable,fail_enable,fail_rtkit_stop;
static bool no_completion,flush_error;
static bool stop_failed_latched,asc_running,reset_failed,csts_fatal;
static u64 now,last_status_address;
static u32 cc,tcb_status;
static u64 cap=BIT(37)|63;
static int adt;
static u64 timeout_calculate(u32 usec) {return now+usec;}
static bool timeout_expired(u64 deadline) {return ++now>deadline;}
static void *test_alloc(size_t n) {live_allocations++;total_allocations++;return calloc(1,n);}
static void *memalign(size_t alignment,size_t n) {total_allocations++;void *p;assert(!posix_memalign(&p,alignment,n));live_allocations++;return p;}
static void test_free(void *p) {if(p){assert(live_allocations);live_allocations--;free(p);}}
#define free test_free
static int adt_path_offset_trace(int a,const char *p,int *out) {(void)a;(void)p;(void)out;return 1;}
static int adt_get_reg(int a,int *p,const char *name,int i,u64 *base,void *size) {(void)a;(void)p;(void)name;(void)i;(void)size;*base=0x100000;return 0;}
#define ADT_GETPROP(a,b,c,d) ((void)(a),(void)(b),(void)(c),*(d)=0,0)
static asc_dev_t *asc_init(const char *p) {(void)p;return test_alloc(4);}
static sart_dev_t *sart_init(const char *p) {(void)p;return test_alloc(4);}
static rtkit_dev_t *rtkit_init(const char *n,asc_dev_t *a,void *b,void *c,sart_dev_t *d,bool f) {(void)n;(void)a;(void)b;(void)c;(void)d;(void)f;return test_alloc(4);}
static void asc_free(asc_dev_t *p) {test_free(p);}
static void sart_free(sart_dev_t *p) {test_free(p);}
static void rtkit_free(rtkit_dev_t *p) {test_free(p);}
static bool rtkit_boot(rtkit_dev_t *p) {(void)p;starts++;return boot_ok;}
static bool rtkit_boot_bounded(rtkit_dev_t *p,u64 deadline) {(void)deadline;return rtkit_boot(p);}
static bool rtkit_sleep(rtkit_dev_t *p) {(void)p;return true;}
static bool rtkit_sleep_bounded(rtkit_dev_t *p,u64 deadline,u32 *stage) {(void)p;(void)deadline;stop_calls++;*stage=fail_rtkit_stop?RTKIT_STOP_IOP_ACK:RTKIT_STOP_DONE;return !fail_rtkit_stop;}
static int rtkit_recv(rtkit_dev_t *p,struct rtkit_message *m) {(void)p;(void)m;legacy_polls++;return 0;}
static int rtkit_poll_boot_once(rtkit_dev_t *p,u64 deadline) {(void)p;(void)deadline;boot_polls++;return 0;}
static int rtkit_poll_system_once(rtkit_dev_t *p,u64 deadline) {(void)p;(void)deadline;system_polls++;return 0;}
static bool asc_cpu_running(asc_dev_t *a) {(void)a;return asc_running;}
static int pmgr_reset(u8 die,const char *name) {(void)die;(void)name;return reset_failed?-1:0;}
static void dma_wmb(void) {}
static void dma_rmb(void) {}
static u64 read64(u64 address) {(void)address;return cap;}
static u32 read32(u64 address) {
 if(address==nvme_base+NVME_BOOT_STATUS)return fail_boot_status?0:NVME_BOOT_STATUS_OK;
 if(address==nvme_base+NVME_CC)return cc;
 if(address==nvme_base+NVME_CSTS) {
   if(csts_fatal)return UINT32_MAX;
   return ((fail_disable || ((cc&1)&&!fail_enable))?NVME_CSTS_RDY:0) |
          (((cc&NVME_CC_SHN)&&!fail_shutdown)?FIELD_PREP(NVME_CSTS_SHST,NVME_CSTS_SHST_DONE):0);
 }
 if(address==nvme_base+NVMMU_TCB_STAT_NATIVE||address==nvme_base+NVMMU_TCB_STAT){last_status_address=address;return tcb_status;}
 return 0;
}
static void write32(u64 address,u32 value) {
 if(address==nvme_base+NVME_CC)cc=value;
 if(address==nvme_base+NVME_DB_LINEAR_ASQ||address==nvme_base+NVME_DB_LINEAR_IOSQ) {
   unsigned q=address==nvme_base+NVME_DB_LINEAR_ASQ?0:1;submits++;
   struct ans_hw_queue *queue=&queue_engine.queues[q];
   if(!no_completion)queue->cqes[queue->head]=(struct nvme_completion){.tag=(uint16_t)value,.status=(uint16_t)(queue->phase|(flush_error?2:0))};
 }
}
static void clear32(u64 a,u32 bits) {write32(a,read32(a)&~bits);}
static void set32(u64 a,u32 bits) {write32(a,read32(a)|bits);}
static void mask32(u64 a,u32 mask,u32 bits) {write32(a,(read32(a)&~mask)|bits);}
static void write64_lo_hi(u64 a,u64 value) {(void)a;(void)value;}
static int poll32(u64 a,u32 mask,u32 value,u32 timeout) {(void)timeout;return (read32(a)&mask)==value?0:-1;}
'''
for name in ['alloc_queue','free_queue','nvme_poll_syslog','nvme_ctrl_disable','nvme_ctrl_enable',
             'nvme_ctrl_shutdown','nvme_wait_bounded','nvme_ctrl_disable_bounded','nvme_ctrl_enable_bounded',
             'queue_dma_wmb','queue_dma_rmb','queue_submit','queue_invalidate','queue_ack',
             'nvme_exec_command','nvme_init_common','nvme_init','nvme_native_init','nvme_shutdown',
             'nvme_native_generation','nvme_native_queue_backing','nvme_native_submit','nvme_native_poll',
             'nvme_stop_bounded','nvme_retire_stopped','nvme_recovery_physical_idle','nvme_recovery_idle',
             'nvme_begin_native_handoff','nvme_handoff_idle','nvme_handoff_flush_bounded','nvme_native_tcb_inval_failures',
             'nvme_flush','nvme_rw_blocks','nvme_read_blocks','nvme_write_blocks','nvme_read','nvme_write','nvme_get_namespace_info']:
    pre+='\n'+fn(name)
pre+=r'''
static void reset_flags(void) {
 assert(!live_allocations);starts=submits=system_polls=stop_calls=boot_polls=legacy_polls=0;tcb_status=0;
 no_completion=flush_error=false;nvme_native_reserved=true; /* stopped native-world fixture */now=last_status_address=cc=0;
 boot_ok=true;fail_boot_status=fail_shutdown=fail_disable=fail_enable=fail_rtkit_stop=false;
 stop_failed_latched=asc_running=reset_failed=csts_fatal=false;cap=BIT(37)|63;
}
int main(void) {
 u32 stage;u64 sq,cq;struct nvme_completion cqe;
 assert(!nvme_native_init(0,1000,&stage)&&stage==NVME_STAGE_INVALID);
 assert(!nvme_native_init(77,1000,&stage)); /* no reservation */
 nvme_native_reserved=true;
 assert(nvme_native_init(77,1000,&stage)&&stage==NVME_STAGE_READY);
 assert(nvme_native_generation()==77&&submits==0&&live_allocations==9);
 assert(cc==0x00460001); /* Native must not inherit firmware CC fields. */
 assert(!nvme_init()&&!nvme_recovery_idle());
 unsigned old_allocations=total_allocations,old_submits=submits;u64 blocks;u32 block_size;
 u64 old_prps[512];memcpy(old_prps,io_prp_list,sizeof(old_prps));
 assert(!nvme_read(1,0,(void*)0x20000)&&!nvme_write(1,0,(void*)0x20000));
 assert(!nvme_read_blocks(1,0,3,(void*)0x20000)&&!nvme_write_blocks(1,0,3,(void*)0x20000));
 assert(!nvme_flush(1)&&!nvme_get_namespace_info(1,&blocks,&block_size));
 assert(total_allocations==old_allocations&&submits==old_submits&&!memcmp(old_prps,io_prp_list,sizeof(old_prps)));
 assert(nvme_native_queue_backing(1,&sq,&cq)&&sq==(u64)ioq.cmds&&cq==(u64)ioq.cqes);
 struct nvme_command cmd={.opcode=6,.prp1=0xdead000,.cdw12=0x4000001f};
 assert(nvme_native_submit(76,0,0,&cmd,1)==ANS_Q_STALE);
 assert(nvme_native_submit(77,0,0,&cmd,3)==ANS_Q_INVALID);
 assert(nvme_native_submit(77,0,0,&cmd,1)==ANS_Q_OK);
 assert(adminq.tcbs[0].opcode==0&&adminq.tcbs[0].dma_flags==1&&adminq.tcbs[0].len==31);
 assert(nvme_native_poll(77,0,&cqe)==ANS_Q_OK);
 assert(last_status_address==nvme_base+0x28120);
 assert(nvme_native_tcb_inval_failures()==0);
 /* ANS006: like Asahi apple.c apple_nvmmu_inval, a nonzero TCB status is advisory (counted),
  * never fatal: treating it as a queue fault broke sustained native I/O on J313. */
 tcb_status=5;assert(queue_invalidate(NULL,0));assert(nvme_native_tcb_inval_failures()==1);
 assert(nvme_native_tcb_inval_failures()==1); /* reads do not mutate telemetry */
 tcb_status=0;assert(queue_invalidate(NULL,0)&&nvme_native_tcb_inval_failures()==1);
 assert(nvme_stop_bounded(1000,&stage)&&stage==NVME_STAGE_STOPPED&&live_allocations==9);
 assert(!nvme_native_generation()&&nvme_native_submit(77,0,0,&cmd,1)==ANS_Q_FAULT);
 assert(nvme_stop_bounded(1000,&stage)&&stop_calls==1); /* idempotent receipt */
 assert(nvme_retire_stopped()&&!live_allocations&&nvme_native_tcb_inval_failures()==1);
 reset_flags();assert(nvme_native_init(78,1000,&stage));
 assert(nvme_native_submit(78,0,0,&cmd,1)==0);
 assert(!nvme_stop_bounded(1000,&stage)&&stage==NVME_STAGE_DRAIN&&live_allocations==9);
 assert(queue_engine.outstanding==1&&!nvme_retire_stopped());
 nvme_shutdown();assert(live_allocations==9); /* legacy path cannot free native faults */
 /* Test teardown is explicitly synthetic; production has no such bypass. */
 queue_engine.outstanding=0;
 assert(nvme_stop_bounded(1000,&stage)&&nvme_retire_stopped());
 for(unsigned failure=0;failure<4;failure++) {
   reset_flags();assert(nvme_native_init(90+failure,1000,&stage));
   if(failure==0)fail_shutdown=true;
   if(failure==1)fail_disable=true;
   if(failure==2)fail_rtkit_stop=true;
   if(failure==3)nvme_native_boot_failed=true;
   if(failure<3) {
     assert(!nvme_stop_bounded(30,&stage)&&live_allocations==9&&queue_engine.faulted);
     assert(stage==(failure==0?NVME_STAGE_SHUTDOWN:failure==1?NVME_STAGE_DISABLE:NVME_STAGE_RTKIT_IOP_ACK));
     nvme_shutdown();assert(live_allocations==9&&!nvme_retire_stopped());
   }
   fail_shutdown=fail_disable=fail_rtkit_stop=false;
   assert(nvme_stop_bounded(1000,&stage)&&nvme_retire_stopped());
 }
 reset_flags();boot_ok=false;
 assert(!nvme_native_init(123,1000,&stage)&&stage==NVME_STAGE_RTKIT_BOOT);
 assert(live_allocations==9&&nvme_native_boot_failed&&queue_engine.faulted);
 nvme_shutdown();assert(live_allocations==9);
 assert(nvme_stop_bounded(1000,&stage)&&nvme_retire_stopped());
 for(unsigned fault=0;fault<2;fault++) {
   reset_flags();asc_running=fault==0;reset_failed=fault==1;
   assert(!nvme_native_init(125+fault,1000,&stage)&&stage==NVME_STAGE_SOFT_RESET);
   assert(starts==0&&live_allocations==9);
   asc_running=reset_failed=false;
   assert(nvme_stop_bounded(1000,&stage)&&nvme_retire_stopped());
 }
 reset_flags();csts_fatal=true;
 assert(!nvme_native_init(129,1000,&stage)&&stage==NVME_STAGE_DISABLE);
 assert(live_allocations==9);csts_fatal=false;
 assert(nvme_stop_bounded(1000,&stage)&&nvme_retire_stopped());
 for(unsigned badcap=0;badcap<4;badcap++) {
   reset_flags();cap=badcap==0?63:badcap==1?(BIT(37)|BIT(48)|63):badcap==2?(BIT(37)|BIT(32)|63):BIT(37);
   assert(!nvme_native_init(130+badcap,1000,&stage)&&stage==NVME_STAGE_ENABLE);
   assert(!(cc&1)&&live_allocations==9);
   assert(nvme_stop_bounded(1000,&stage)&&nvme_retire_stopped());
 }
 reset_flags();nvme_native_reserved=false;assert(nvme_init()&&submits==2&&live_allocations==9);
 assert(nvme_recovery_idle());
 queue_engine.outstanding=1;assert(!nvme_recovery_idle());queue_engine.outstanding=0;
 queue_engine.faulted=true;assert(!nvme_recovery_idle());queue_engine.faulted=false;
 nvme_stop_failed=true;assert(!nvme_recovery_idle());nvme_stop_failed=false;
 tcb_status=1;assert(queue_invalidate(NULL,0)&&nvme_native_tcb_inval_failures()==1);tcb_status=0;
 assert(adminq.tcbs[0].dma_flags==3&&last_status_address==nvme_base+0x29120);
 /* Stop failure also protects recovery backing from legacy teardown. */
 fail_shutdown=true;
 assert(!nvme_stop_bounded(30,&stage));nvme_shutdown();assert(live_allocations==9);
 assert(!nvme_init());fail_shutdown=false;
 assert(nvme_stop_bounded(1000,&stage)&&nvme_retire_stopped());
 reset_flags();nvme_native_reserved=false;assert(nvme_init());nvme_shutdown();assert(!live_allocations&&!nvme_recovery_idle());
 /* Exercise actual register wait's dispatcher choice with a not-ready status. */
 reset_flags();fail_boot_status=true;
 assert(!nvme_wait_bounded(NVME_BOOT_STATUS,UINT32_MAX,NVME_BOOT_STATUS_OK,30,true));
 assert(boot_polls>0&&!system_polls);
 now=0;boot_polls=system_polls=0;
 assert(!nvme_wait_bounded(NVME_BOOT_STATUS,UINT32_MAX,NVME_BOOT_STATUS_OK,30,false));
 assert(system_polls>0&&!boot_polls);
 (void)io_prp_list;
 /* Reserve through the actual recovery-idle boundary, before any flush. */
 for(unsigned failure=0;failure<3;failure++) {
   reset_flags();nvme_native_reserved=false;assert(nvme_init());
   assert(nvme_recovery_idle()&&!nvme_handoff_idle());
   assert(nvme_begin_native_handoff()&&!nvme_begin_native_handoff());
   assert(nvme_handoff_idle()&&!nvme_recovery_idle()&&!nvme_init());
   old_allocations=total_allocations;old_submits=submits;
   memcpy(old_prps,io_prp_list,sizeof(old_prps));
   assert(!nvme_read_blocks(1,0,3,(void*)0x20000)&&!nvme_write_blocks(1,0,3,(void*)0x20000));
   assert(!nvme_flush(1)&&!nvme_get_namespace_info(1,&blocks,&block_size));
   nvme_shutdown();assert(live_allocations==9&&total_allocations==old_allocations&&submits==old_submits);
   assert(!memcmp(old_prps,io_prp_list,sizeof(old_prps)));
   no_completion=failure==1;flush_error=failure==2;tcb_status=7;
   unsigned old_legacy=legacy_polls,old_prints=prints;
   bool flushed=nvme_handoff_flush_bounded(1,30,&stage);
   assert(flushed==(failure==0)&&legacy_polls==old_legacy&&prints==old_prints&&!nvme_bounded_submission);
   assert(ioq.tcbs[0].opcode==0&&ioq.tcbs[0].dma_flags==3&&ioq.cmds[0].prp1==0);
   if(failure==0)assert(stage==NVME_STAGE_FLUSH_DONE&&nvme_handoff_idle());
   else assert(stage==NVME_STAGE_FLUSH_COMPLETION&&queue_engine.faulted&&!nvme_handoff_idle());
   if(failure==1)assert(queue_engine.outstanding==1&&system_polls>0);
   /* Only synthetic cleanup can discard an uncompleted model request. */
   queue_engine.outstanding=0;no_completion=flush_error=false;tcb_status=0;
   assert(nvme_stop_bounded(1000,&stage)&&nvme_retire_stopped());
   assert(nvme_native_reserved&&!nvme_init()&&!nvme_recovery_idle());
 }
 puts("NVMe native lifecycle/admin-only/policy/failure-retention tests PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='nvme-native-lifecycle-') as d:
    c=Path(d)/'test.c';exe=Path(d)/'test';c.write_text(pre)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-g','-I',str(ROOT/'src'),str(c),
                    str(ROOT/'src/ans_queue.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
