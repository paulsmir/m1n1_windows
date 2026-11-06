/* SPDX-License-Identifier: MIT */
#include "hv_ans_platform.h"
#include "hv_ans_service.h"
#include "hv_ans_memory.h"
#include "hv_ans_pins.h"
#include "hv.h"
#include "hv_irq_routes.h"
#include "hv_launch_j313.h"
#include "heapblock.h"
#include "hv_autonomous_layout.generated.h"
#include "adt.h"
#include "exception.h"
#include "nvme.h"
#include "smp.h"
#include "soc.h"
#include "string.h"
#include "utils.h"
#include "xnuboot.h"

extern bool hv_nvme_freeze_recovery(void);
extern bool hv_nvme_recovery_drained(void);
extern bool hv_nvme_flush_frozen(void);
extern bool hv_nvme_recovery_geometry(u64 *blocks);
extern void hv_nvme_seal_recovery(void);
extern void hv_pci_hide_nvme(void);
extern void hv_quarantine_guest(void) __attribute__((noreturn));
extern uint64_t vaddr_bits;
static DECLARE_SPINLOCK(ans_lock);
static DECLARE_SPINLOCK(ans_mapping_lock);
static struct {
    struct ans_service service;
    struct ans_pin_set pins;
    struct ans_memory_context memory;
    struct hv_contract_snapshot launch;
    struct ans_memory_range private[ANS_MEMORY_MAX_PRIVATE_RANGES];
    struct ans_broker_request request;
    struct ans_broker_response response;
    bool mapped, memory_ready;
    unsigned active_tag;
    u32 physical_irq;
    u64 physical_irqs, pending_snapshot;
    struct ans_telemetry telemetry_snapshot;
    u32 request_words;
} runtime;
static u64 shadow_words[ANS_QUEUE_DEPTH][ANS_PRP_PAGE_SIZE / 8] ALIGNED(ANS_PRP_PAGE_SIZE);

bool hv_ans_mapping_begin(u64 ipa,u64 size)
{
    spin_lock(&ans_mapping_lock);
    if(!ans_pin_mapping_allowed(&runtime.pins,ipa,size)) {
        spin_unlock(&ans_mapping_lock);return false;
    }
    return true;
}
void hv_ans_mapping_end(void){spin_unlock(&ans_mapping_lock);}
static bool raw_leaf(void*ctx,uint64_t ipa,uint64_t*raw,uint8_t*shift)
{ UNUSED(ctx);return hv_stage2_get_leaf(ipa,raw,shift); }
#define ANS_CARVEOUT_IDS 256u
static struct ans_memory_range carveout_scratch[ANS_CARVEOUT_IDS];
/* iBoot-retained memory (ANSDART001-R3: 13 carveouts inside GUEST_RAM; every
 * observed retained DART table page lies in a carveout). Excluding them from
 * ANS DMA also bounds DARTs whose registers cannot be read safely. */
static bool load_carveout_exclusions(size_t *count)
{
    int node=adt_path_offset(adt,"/chosen/carveout-memory-map");
    size_t n=0;
    if(node<0)return false;
    for(unsigned id=0;id<ANS_CARVEOUT_IDS;id++){
        char name[24];u32 len=0;
        snprintf(name,sizeof(name),"region-id-%u",id);
        const u64*v=adt_getprop(adt,node,name,&len);
        if(!v)continue;
        if(len!=16)return false;
        carveout_scratch[n++]=(struct ans_memory_range){v[0],v[1]};
    }
    const struct hv_contract_region*g=NULL;
    for(unsigned i=0;i<runtime.launch.region_count;i++)
        if(runtime.launch.regions[i].kind==HV_CONTRACT_REGION_GUEST_RAM)g=&runtime.launch.regions[i];
    if(!g)return false;
    return ans_memory_carveout_exclusions(carveout_scratch,n,
        (struct ans_memory_range){g->base,g->size},
        (struct ans_memory_range){cur_boot_args.phys_base,cur_boot_args.mem_size},
        runtime.private,ANS_MEMORY_MAX_PRIVATE_RANGES,count);
}
/* DART_TABLES (flags 0, size 0 inside GUEST_RAM) is truthful only while the
 * m1n1 heap is sealed exactly at the guest base (proxy.c seal_heap_below_guest)
 * and the carveout exclusions are loaded. HYP-ANS-CARVEOUT: every iBoot-retained
 * page is declared in /chosen/carveout-memory-map (ANSDART001-R3, HVS2ISO002). */
static bool heap_sealed_below_guest(void)
{
    for(unsigned i=0;i<runtime.launch.region_count;i++)
        if(runtime.launch.regions[i].kind==HV_CONTRACT_REGION_GUEST_RAM)
            return heapblock_get_ceiling() &&
                   (u64)heapblock_get_ceiling()==runtime.launch.regions[i].base;
    return false;
}
static bool prepare_memory(void)
{
    size_t private_count=2;
    if(!hv_launch_j313_capture_base(HV_CONTRACT_PRE_GUEST,1,&runtime.launch))return false;
    runtime.private[0]=(struct ans_memory_range){(u64)&runtime,sizeof(runtime)};
    runtime.private[1]=(struct ans_memory_range){(u64)shadow_words,sizeof(shadow_words)};
    if(!load_carveout_exclusions(&private_count)||!heap_sealed_below_guest()){
        runtime.memory_ready=false;return false;
    }
    runtime.memory=(struct ans_memory_context){
        .launch=&runtime.launch,.private_pa=runtime.private,.private_count=private_count,
        .ramdisk={J313_AUTONOMOUS_LAYOUT.ramdisk_base,J313_AUTONOMOUS_LAYOUT.ramdisk_max_size},
        .reservations_complete=true,
        /* proven just above: heapblock ceiling == GUEST_RAM base (ANS006-R6) */
        .heap_sealed_below_guest=true,.ipa_bits=(uint8_t)vaddr_bits,.raw_leaf=raw_leaf};
    /* The authorization helper separately checks every required reservation kind
     * and all source bounds; a missing/incomplete descriptor remains fail closed. */
    runtime.memory_ready=ans_memory_context_valid(&runtime.memory);return runtime.memory_ready;
}
static bool translate(void*ctx,uint64_t ipa,size_t size,uint32_t direction,uint64_t*pa)
{
    UNUSED(ctx);
    static unsigned deny_logs;
    if(!runtime.memory_ready||runtime.active_tag>=ANS_QUEUE_DEPTH)return false;
    if(!ans_memory_translate(&runtime.memory,ipa,size,direction,pa)) {
        /* Receipt-only (ANS006): which guest page the EL2 DMA policy rejected and why. */
        if(deny_logs<16) {
            uint64_t raw=0;uint8_t shift=0;bool leaf=raw_leaf(NULL,ipa,&raw,&shift);
            deny_logs++;
            printf("HV ANS deny ipa=0x%lx size=0x%lx dir=%u leaf=%u raw=0x%lx shift=%u oa=0x%lx\n",
                   (unsigned long)ipa,(unsigned long)size,(unsigned)direction,(unsigned)leaf,
                   (unsigned long)raw,(unsigned)shift,(unsigned long)(raw&0xfffffffff000ULL));
        }
        return false;
    }
    if(!ans_pin_add(&runtime.pins,runtime.active_tag,ipa,size)) {
        if(deny_logs<16){deny_logs++;printf("HV ANS deny pin-table ipa=0x%lx size=0x%lx tag=%u\n",
            (unsigned long)ipa,(unsigned long)size,(unsigned)runtime.active_tag);}
        return false;
    }
    return true;
}
static bool read_metadata(void*ctx,uint64_t pa,void*out,size_t size)
{ UNUSED(ctx);if(size>ANS_MAX_DATA_PAGES*8)return false;memcpy(out,(const void*)pa,size);return true; }
static bool freeze(void*ctx){UNUSED(ctx);return hv_nvme_freeze_recovery();}
static bool drain(void*ctx){UNUSED(ctx);return hv_nvme_recovery_drained();}
static bool flush(void*ctx){UNUSED(ctx);return hv_nvme_flush_frozen();}
static bool stop(void*ctx,uint32_t*stage)
{
    UNUSED(ctx);
    bool ok=nvme_stop_bounded(5000000,stage);
    if(!ok) {
        /* A pending guest-DMA grant cannot be released by Storport after a
         * failed initialization/reset. Containment belongs here in EL2. */
        for(unsigned t=0;t<ANS_QUEUE_DEPTH;t++)
            if(runtime.pins.counts[t])hv_quarantine_guest();
    }
    return ok;
}
static bool retire(void*ctx){UNUSED(ctx);return nvme_retire_stopped();}
static bool hide(void*ctx)
{
    UNUSED(ctx);
    /* The service calls this only after recovery DMA stop and retirement.
     * Hiding that retired frontend does not authorize any guest DMA by itself:
     * prepare_memory() admits memory only with the heap sealed at the guest
     * base and the ADT carveouts excluded; otherwise boot()/translate() stay
     * closed and only QUERY/CLAIM remain available. */
    hv_nvme_seal_recovery();hv_pci_hide_nvme();
    (void)prepare_memory();
    return true;
}
static bool boot(void*ctx,uint64_t generation,uint32_t*stage)
{ UNUSED(ctx);return runtime.memory_ready&&nvme_native_init(generation,5000000,stage); }
static bool backing(void*ctx,unsigned q,uint64_t*sq,uint64_t*cq)
{ UNUSED(ctx);return nvme_native_queue_backing(q,sq,cq); }
static int submit(void*ctx,uint64_t gen,unsigned q,unsigned tag,const struct nvme_command*c,uint32_t dir)
{ UNUSED(ctx);return nvme_native_submit(gen,q,tag,c,dir); }
static int poll(void*ctx,uint64_t gen,unsigned q,struct nvme_completion*c)
{ UNUSED(ctx);return nvme_native_poll(gen,q,c); }
static bool shadow(void*ctx,unsigned tag,const uint64_t*pages,unsigned count,uint64_t*pa)
{
    UNUSED(ctx);
    if(tag>=ANS_QUEUE_DEPTH||!count||count>=ANS_MAX_DATA_PAGES)return false;
    memset(shadow_words[tag],0,sizeof(shadow_words[tag]));
    memcpy(shadow_words[tag],pages,count*sizeof(*pages));*pa=(u64)shadow_words[tag];return true;
}
static void release(void*ctx,unsigned tag)
{
    UNUSED(ctx);if(tag>=ANS_QUEUE_DEPTH)return;
    ans_pin_release(&runtime.pins,tag);memset(shadow_words[tag],0,sizeof(shadow_words[tag]));
}
static bool inside(u64 off,u64 bytes,u64 base,u64 size)
{ return off>=base && bytes<=size && off-base<=size-bytes; }

static bool broker_mmio(struct exc_info*ctx,u64 addr,u64*value,bool write,int width)
{
    UNUSED(ctx);
    u64 off=addr-ANS_BROKER_BASE;
    if(width<0||width>4)hv_quarantine_guest();
    if(width==4){if(!write)memset(value,0,16);runtime.request_words=0;return true;}
    u64 bytes=1ULL<<width;
    if(!write)*value=0;
    if(off>=ANS_BROKER_SIZE||bytes>ANS_BROKER_SIZE-off||(off&(bytes-1)))return true;
    if(!write)*value=0;
    if(!runtime.mapped)return true;
    if(write && off==ANS_ABORT_OFFSET && width==2)hv_quarantine_guest();
    if(write && inside(off,bytes,ANS_REQUEST_OFFSET,sizeof(runtime.request))) {
        if(width!=2){runtime.request_words=0;return true;}
        if(off==ANS_REQUEST_OFFSET)runtime.request_words=0;
        runtime.request_words|=1u<<((off-ANS_REQUEST_OFFSET)/4);
        memcpy((u8*)&runtime.request+off-ANS_REQUEST_OFFSET,value,bytes);return true;
    }
    if(write && off==ANS_HANDOFF_REQUEST_OFFSET && width==3) {
        ans_service_handoff(&runtime.service,*value);return true;
    }
    if(write && off==ANS_DOORBELL_OFFSET && width==2) {
        /* All subsequent validation uses this immutable EL2 snapshot, never the
         * mailbox being filled by guest writes on another CPU. */
        struct ans_broker_request request=runtime.request;
        if((u32)request.sequence!=(u32)*value || runtime.request_words!=((1u<<(sizeof(request)/4))-1u)) {
            ans_service_record_rejection(&runtime.service,ANS_RESULT_BAD_SEQUENCE);
            runtime.response=(struct ans_broker_response){.sequence=request.sequence,
                .generation=runtime.service.broker.header.generation,.result=ANS_RESULT_BAD_SEQUENCE,
                .state=runtime.service.broker.header.state,.queue=request.queue,.tag=request.tag};
            return true;
        }
        runtime.request_words=0;
        runtime.active_tag=request.tag;
        ans_service_execute(&runtime.service,&request,&runtime.response);
        runtime.active_tag=ANS_QUEUE_DEPTH;
        /* Receipt-only (ANS002): control operations are rare; data-path submit/reap are
         * never logged. Capped so a looping guest cannot flood the UART. */
        static unsigned control_logs,data_logs;
        static unsigned fail_logs;
        u32 cqe_dw3=(u32)runtime.response.cqe[12]|((u32)runtime.response.cqe[13]<<8)|
                    ((u32)runtime.response.cqe[14]<<16)|((u32)runtime.response.cqe[15]<<24);
        bool data_op=request.operation==ANS_OP_AUTHORIZE_SUBMIT || request.operation==ANS_OP_REAP_COMPLETION;
        /* ANS006: an error status is bits 31:17 of CQE DW3. Log the first 16 data operations and
         * up to 48 failing ones (result!=0 or CQE status!=0) so late errors are not hidden. */
        bool failed=runtime.response.result!=0 || (request.operation==ANS_OP_REAP_COMPLETION && (cqe_dw3>>17));
        static unsigned big_logs;
        bool big=request.operation==ANS_OP_AUTHORIZE_SUBMIT && request.transfer_length>16384 && big_logs<16;
        if(data_op && (data_logs<16 || big || (failed && fail_logs<48))) {
            if(data_logs<16)data_logs++; else if(failed)fail_logs++; else big_logs++;
            printf("HV ANS data op=%u seq=%lu q=%u tag=%u sqe_op=0x%x len=%u result=%u cqe_dw3=0x%x prp_fail=%lu tcb_stat_nz=%lu\n",
                   (unsigned)request.operation,(unsigned long)request.sequence,(unsigned)request.queue,
                   (unsigned)request.tag,(unsigned)request.sqe[0],(unsigned)request.transfer_length,
                   (unsigned)runtime.response.result,cqe_dw3,
                   (unsigned long)runtime.service.telemetry.prp_translation_failures,
                   (unsigned long)nvme_native_tcb_inval_failures());
        }
        if(request.operation!=ANS_OP_AUTHORIZE_SUBMIT && request.operation!=ANS_OP_REAP_COMPLETION &&
           control_logs<64) {
            control_logs++;
            printf("HV ANS op=%u seq=%lu result=%u state=%u owner=%u gen=%lu\n",
                   (unsigned)request.operation,(unsigned long)request.sequence,
                   (unsigned)runtime.response.result,(unsigned)runtime.response.state,
                   (unsigned)runtime.service.broker.owner,(unsigned long)runtime.response.generation);
        }
        return true;
    }
    if(write)return true; /* All receipts, counters, cached CQEs are read-only. */
    const void*source=NULL;u64 base=0;
    if(inside(off,bytes,0,sizeof(runtime.service.broker.header)))source=&runtime.service.broker.header;
    else if(inside(off,bytes,ANS_RESPONSE_OFFSET,sizeof(runtime.response))){source=&runtime.response;base=ANS_RESPONSE_OFFSET;}
    else if(inside(off,bytes,ANS_TELEMETRY_OFFSET,sizeof(runtime.telemetry_snapshot))) {
        /* Low DWORD latches metadata only; remaining reads are immutable even
         * when IRQ counters advance between guest MMIO accesses. */
        if(off==ANS_TELEMETRY_OFFSET) {
            ans_service_telemetry_snapshot(&runtime.service,&runtime.telemetry_snapshot);
            runtime.telemetry_snapshot.physical_irqs=__atomic_load_n(&runtime.physical_irqs,__ATOMIC_RELAXED);
            runtime.telemetry_snapshot.tcb_inval_failures=nvme_native_tcb_inval_failures();
            if(runtime.telemetry_snapshot.physical_irqs!=UINT64_MAX)
                runtime.telemetry_snapshot.valid_fields|=ANS_TELEM_PHYSICAL_IRQS;
            if(runtime.telemetry_snapshot.tcb_inval_failures!=UINT64_MAX)
                runtime.telemetry_snapshot.valid_fields|=ANS_TELEM_TCB_INVAL_FAILURES;
        }
        source=&runtime.telemetry_snapshot;base=ANS_TELEMETRY_OFFSET;
    }
    else if(inside(off,bytes,ANS_HANDOFF_RECEIPT_OFFSET,sizeof(runtime.service.handoff))){source=&runtime.service.handoff;base=ANS_HANDOFF_RECEIPT_OFFSET;}
    else if(inside(off,bytes,ANS_PENDING_OFFSET,8)) {
        if(off==ANS_PENDING_OFFSET) {
            if(runtime.service.broker.queues_registered)ans_service_drain(&runtime.service);
            runtime.pending_snapshot=runtime.service.pending;
        }
        source=&runtime.pending_snapshot;base=ANS_PENDING_OFFSET;
    } else if(inside(off,bytes,ANS_COMPLETION_OFFSET,ANS_QUEUE_DEPTH*16)) {
        unsigned tag=(off-ANS_COMPLETION_OFFSET)/16;
        u64 within=(off-ANS_COMPLETION_OFFSET)%16;
        if(bytes>16-within)return false;
        if(runtime.service.pending&(1ULL<<tag))memcpy(value,runtime.service.broker.tags[tag].cqe+within,bytes);
        return true;
    }
    if(source)memcpy(value,(const u8*)source+off-base,bytes);
    return true;
}
static bool private_mmio(struct exc_info*ctx,u64 addr,u64*value,bool write,int width)
{ UNUSED(ctx);UNUSED(addr);UNUSED(width);if(!write)*value=0;return true; }

#define ANS_REJECT(why) do{printf("HV ANS broker preflight rejected: %s\n",(why));return false;}while(0)
/* Live J313 ADT /arm-io/ans reg windows (reg0 = ASC incl. mailbox, reg3 = NVMe). */
#define ANS_J313_ADT_COPROCESSOR_SIZE 0x6c000ULL
#define ANS_J313_ADT_CONTROLLER_SIZE 0x60000ULL
_Static_assert(ANS_J313_ADT_COPROCESSOR_SIZE>=ANS_EXPECTED_COPROCESSOR_SIZE&&
               ANS_EXPECTED_MAILBOX_BASE+ANS_EXPECTED_MAILBOX_SIZE<=ANS_EXPECTED_COPROCESSOR_BASE+ANS_J313_ADT_COPROCESSOR_SIZE,
               "ADT ASC window must contain the DT ASC and mailbox windows");
_Static_assert(ANS_J313_ADT_CONTROLLER_SIZE>=ANS_EXPECTED_CONTROLLER_SIZE,"ADT NVMe window must contain the DT window");
bool hv_map_ans_broker(u64 base,u64 size,u32 irq,u32 abi_major)
{
    if(base!=ANS_BROKER_BASE||size!=ANS_BROKER_SIZE||irq!=ANS_GUEST_GSIV||abi_major!=ANS_ABI_MAJOR)ANS_REJECT("broker ABI arguments");
    if(runtime.mapped||hv_guest_has_started()||chip_id!=T8103)ANS_REJECT("state or chip");
    int ap[8],sp[8];
    int ans=adt_path_offset_trace(adt,"/arm-io/ans",ap);
    if(ans<0||adt_path_offset_trace(adt,"/arm-io/sart-ans",sp)<0)ANS_REJECT("ADT nodes");
    u64 controller,controller_size,asc,asc_size,sart,sart_size;
    if(adt_get_reg(adt,ap,"reg",3,&controller,&controller_size)<0||
       adt_get_reg(adt,ap,"reg",0,&asc,&asc_size)<0||adt_get_reg(adt,sp,"reg",0,&sart,&sart_size)<0)
        ANS_REJECT("ADT reg read");
    /* Bases are the source-derived T8103 values. Sizes are the live J313 ADT reg
     * windows (ANSDART001-R3 adt.bin); they contain the smaller DT protocol windows
     * and are hidden from the guest in full by the hooks below. ANS001-R3 attempt 4
     * proved that the DT sizes never match the ADT. No device access in this gate. */
    if(controller!=ANS_EXPECTED_CONTROLLER_BASE||controller_size!=ANS_J313_ADT_CONTROLLER_SIZE||asc!=ANS_EXPECTED_COPROCESSOR_BASE||
       asc_size!=ANS_J313_ADT_COPROCESSOR_SIZE||sart!=ANS_EXPECTED_SART_BASE||sart_size!=ANS_EXPECTED_SART_SIZE){
        printf("HV ANS broker preflight: ADT reg mismatch ctrl %lx+%lx asc %lx+%lx sart %lx+%lx\n",
               (unsigned long)controller,(unsigned long)controller_size,(unsigned long)asc,(unsigned long)asc_size,
               (unsigned long)sart,(unsigned long)sart_size);
        return false;
    }
    if(asc+0x8000!=ANS_EXPECTED_MAILBOX_BASE)ANS_REJECT("mailbox base");
    u32 len=0;const u32*irqs=adt_getprop(adt,ans,"interrupts",&len);unsigned matches=0;
    if(!irqs||!len||len>256||len%4)ANS_REJECT("ADT interrupts");
    for(unsigned i=0;i<len/4;i++)if(irqs[i]==ANS_EXPECTED_PHYSICAL_IRQ)matches++;
    if(matches!=1)ANS_REJECT("physical IRQ not unique");
    u64 blocks;
    if(!hv_nvme_recovery_geometry(&blocks))ANS_REJECT("recovery geometry");
    struct ans_service_ops ops={freeze,drain,flush,stop,retire,hide,boot,backing,submit,poll,shadow,release};
    struct ans_memory_ops mem={translate,read_metadata};
    if(!ans_service_init(&runtime.service,&ops,NULL,&mem,NULL,blocks))ANS_REJECT("service init");
    runtime.active_tag=ANS_QUEUE_DEPTH;runtime.physical_irq=ANS_EXPECTED_PHYSICAL_IRQ;
    if(!hv_irq_route_register(runtime.physical_irq,ANS_GUEST_GSIV,true))ANS_REJECT("IRQ route");
    if(hv_map_hook(controller,private_mmio,controller_size)||hv_map_hook(asc,private_mmio,asc_size)||
       hv_map_hook(sart,private_mmio,sart_size)||
       hv_map_hook(ANS_EXPECTED_MAILBOX_BASE,private_mmio,ANS_EXPECTED_MAILBOX_SIZE)||hv_map_hook(ANS_BROKER_BASE,broker_mmio,ANS_BROKER_SIZE))ANS_REJECT("MMIO hooks");
    runtime.service.broker.header.features|=ANS_FEATURE_TELEMETRY;
    runtime.mapped=true;return true;
}

bool hv_ans_try_handle_dabort(struct exc_info*ctx,bool*matched)
{
    if(matched)*matched=false;
    if(!runtime.mapped)return false;
    bool write=!!(ctx->esr&ESR_ISS_DABORT_WnR);
    u64 ipa=hv_translate(ctx->far,true,write,NULL);
    if(ipa<ANS_BROKER_BASE||ipa-ANS_BROKER_BASE>=ANS_BROKER_SIZE)return false;
    /* Like recovery NVMe, long initialization/stop never holds global BHL.
     * Other CPUs retain their independent timer/IRQ progress. */
    spin_lock(&ans_lock);spin_lock(&ans_mapping_lock);
    if(matched)*matched=true;
    bool result=hv_handle_dabort(ctx);
    spin_unlock(&ans_mapping_lock);spin_unlock(&ans_lock);return result;
}
void hv_ans_note_physical_irq(u32 irq)
{
    if(runtime.mapped&&irq==runtime.physical_irq)
    {
        u64 count=__atomic_load_n(&runtime.physical_irqs,__ATOMIC_RELAXED);
        while(count!=UINT64_MAX && !__atomic_compare_exchange_n(&runtime.physical_irqs,&count,
            count+1,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) {}
    }
}
