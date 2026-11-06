#!/usr/bin/env python3
"""Actual assisted descriptor -> production publish/capture -> ANS memory/handoff.
WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
No-driver ownership handoff must not imply permission for DMA when the actual
assisted descriptor explicitly leaves DART table reservations UNOBSERVED.
"""
from pathlib import Path
import ast,hashlib,subprocess,sys,tempfile
from types import SimpleNamespace as NS
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.j313_launch_descriptor import pack_descriptor
from guest_layout import load_layout
layout=load_layout(ROOT/'config/j313-guest-layout.json')
runner=ast.parse((ROOT/'run_uefi.py').read_text())
assignment=next(n for n in runner.body if isinstance(n,ast.Assign) and
                any(isinstance(t,ast.Name) and t.id=='descriptor' for t in n.targets))
# Evaluate only the real descriptor assignment, with explicit synthetic boot
# observation inputs. No runner imports, USB endpoints or primary-source literals
# for unmeasured hardware state are executed by this test.
ctx={'pack_descriptor':pack_descriptor,'hashlib':hashlib,'layout':layout,
 'args':NS(phys_base=layout.phys_base,low_mem_backing=layout.low_mem_pa,low_mem_size=layout.low_mem_size,low_mem=True),
 'mem_top':layout.ram_end,'guest_base':layout.phys_base+0x1020000,
 'bootargs_base':layout.phys_base+0x3020000,'payload_bytes':bytes(0x2000000),
 'u':NS(ba=NS(devtree_size=0x10000)),
 'fb':NS(base=layout.virtual_fb_base,size=layout.virtual_fb_stride*layout.virtual_fb_height,
         width=layout.virtual_fb_width,height=layout.virtual_fb_height,stride=layout.virtual_fb_stride),
 'align':lambda n:(n+0x3fff)&~0x3fff,'dart_base':0,'uart_base':0,'adt_blob':b'synthetic ADT test bytes'}
exec(compile(ast.Module(body=[assignment],type_ignores=[]),'<production descriptor>','exec'),ctx)
source=(ROOT/'m1n1_windows/src/hv_ans_platform.c').read_text()
def function(name):
 start=source.index('static bool '+name+'(');begin=source.index('{',start);end=begin+1;depth=1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]+'\n'
pre=r'''
#include "hv_ans_service.h"
#include "hv_ans_memory.h"
#include "hv_launch_j313.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
#define UNUSED(x) (void)(x)
static struct {
 struct ans_service service;
 struct ans_memory_context memory;
 struct hv_contract_snapshot launch;
 struct ans_memory_range private[ANS_MEMORY_MAX_PRIVATE_RANGES];
 bool memory_ready;
} runtime;
static unsigned char shadow_words[4096];
static uint64_t vaddr_bits=36;
static const struct {uint64_t ramdisk_base,ramdisk_max_size;} J313_AUTONOMOUS_LAYOUT={0x8b0000000,0x40000000};
static unsigned sealed,hidden,boots,stops,carveout_loads;
/* Production reads the live ADT; the boundary under test only needs the call.
 * Exclusion selection itself is covered by hv_ans_memory_test.c. */
static bool load_carveout_exclusions(size_t*count)
{carveout_loads++;runtime.private[(*count)++]=(struct ans_memory_range){0x864204000,0x7a00000};return true;}
static bool stop_ok=true;
static void hv_nvme_seal_recovery(void){sealed++;}
static void hv_pci_hide_nvme(void){hidden++;}
static bool raw_leaf(void*c,uint64_t ipa,uint64_t*raw,uint8_t*shift)
{(void)c;(void)ipa;(void)raw;(void)shift;assert(!"unobserved reservations must reject before page lookup");return false;}
static bool nvme_native_init(uint64_t gen,unsigned timeout,uint32_t*stage)
{(void)gen;(void)timeout;(void)stage;boots++;return true;}
'''
post=r'''
static bool yes(void*c){(void)c;return true;}
static bool stop(void*c,uint32_t*stage){(void)c;*stage=13;stops++;return stop_ok;}
static bool backing(void*c,unsigned q,uint64_t*s,uint64_t*d){(void)c;(void)q;(void)s;(void)d;return false;}
static int submit(void*c,uint64_t g,unsigned q,unsigned t,const struct nvme_command*n,uint32_t d)
{(void)c;(void)g;(void)q;(void)t;(void)n;(void)d;assert(!"payload not admitted");return ANS_Q_FAULT;}
static int poll(void*c,uint64_t g,unsigned q,struct nvme_completion*n)
{(void)c;(void)g;(void)q;(void)n;return ANS_Q_EMPTY;}
static bool shadow(void*c,unsigned t,const uint64_t*p,unsigned n,uint64_t*out)
{(void)c;(void)t;(void)p;(void)n;(void)out;return false;}
static void release(void*c,unsigned t){(void)c;(void)t;}
static bool translate(void*c,uint64_t ipa,size_t n,uint32_t d,uint64_t*out)
{(void)c;return ans_memory_translate(&runtime.memory,ipa,n,d,out);}
static bool metadata(void*c,uint64_t pa,void*out,size_t n){(void)c;(void)pa;(void)out;(void)n;return false;}
int main(int argc,char**argv){
 assert(argc==2);struct hv_launch_j313_descriptor descriptor;
 FILE*f=fopen(argv[1],"rb");assert(f && fread(&descriptor,1,sizeof(descriptor),f)==sizeof(descriptor));fclose(f);
 assert(hv_launch_j313_publish_descriptor(&descriptor));
 assert(!prepare_memory());
 assert(runtime.launch.region_count==8 && runtime.launch.regions[7].kind==HV_CONTRACT_REGION_DART_TABLES);
 assert(runtime.launch.regions[7].flags==1 && runtime.launch.regions[7].size==0);
 for(unsigned i=0;i<7;i++)assert(runtime.launch.regions[i].flags==0 && runtime.launch.regions[i].size>0);
 struct ans_service_ops ops={yes,yes,yes,stop,yes,hide,boot,backing,submit,poll,shadow,release};
 struct ans_memory_ops mem={translate,metadata};
 assert(ans_service_init(&runtime.service,&ops,NULL,&mem,NULL,1000));
 ans_service_handoff(&runtime.service,1);
 assert(ans_handoff_receipt_valid(&runtime.service.handoff,1,0));
 assert(stops==1 && sealed==1 && hidden==1 && !runtime.memory_ready && boots==0);
 assert(carveout_loads==2 && runtime.memory.private_count==3);
 assert(runtime.launch.regions[7].flags==1 && runtime.launch.regions[7].size==0);
 uint64_t pa=123;assert(!ans_memory_translate(&runtime.memory,0x100000,4096,ANS_DMA_FROM_DEVICE,&pa) && pa==0);
 uint32_t stage=0;assert(!boot(NULL,1,&stage) && boots==0);
 struct ans_broker_request req={.abi_major=ANS_ABI_MAJOR,.size=sizeof(req),.generation=1,.sequence=1,.operation=ANS_OP_CLAIM};
 struct ans_broker_response response;
 ans_service_execute(&runtime.service,&req,&response);assert(response.result==0);
 req.sequence++;req.operation=ANS_OP_REGISTER_QUEUES;
 ans_service_execute(&runtime.service,&req,&response);assert(response.result!=0 && boots==0);
 /* Independence does not relax any real stop/retirement prerequisite. */
 struct ans_service failed;assert(ans_service_init(&failed,&ops,NULL,&mem,NULL,1000));
 stop_ok=false;ans_service_handoff(&failed,1);
 assert(!ans_handoff_receipt_valid(&failed.handoff,1,0) && sealed==1 && hidden==1);
 puts("Actual assisted descriptor/capture: unbound handoff independent, native DMA fails closed PASS");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='ans-handoff-memory-') as d:
 d=Path(d);(d/'descriptor.bin').write_bytes(ctx['descriptor'])
 (d/'test.c').write_text(pre+function('prepare_memory')+function('hide')+function('boot')+post)
 sources=['hv_ans_service.c','hv_ans_memory.c','hv_ans_broker.c','hv_ans_prp.c','hv_launch_j313.c','hv_launch_snapshot.c','hv_launch_contract.c']
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
 '-DHV_LAUNCH_J313_HOST_TEST','-DHV_IRQ_ROUTES_HOST_TEST','-I',str(ROOT/'m1n1_windows/src'),
 str(d/'test.c'),*[str(ROOT/'m1n1_windows/src'/s) for s in sources],'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test'),str(d/'descriptor.bin')],check=True)
