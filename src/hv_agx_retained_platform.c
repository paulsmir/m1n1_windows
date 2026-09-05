#include "hv.h"
#include "hv_agx_retained_platform.h"
#include "hv_agx_retained_root.h"
#include "hv_agx_retained_mmio.h"
#include "hv_agx_retained_backing.h"
#include "hv_launch_j313.h"
#include "hv_autonomous_layout.generated.h"
#include "hv_agx_g2.generated.h"
#include "adt.h"
#include "utils.h"
#include "xnuboot.h"
#include <malloc.h>
#include <string.h>

static struct hv_agx_retained_root root_owner;
static struct hv_agx_retained_mmio wire;
static u64 root_base, root_length, asc_base, next_epoch;
static bool request_powered;
static struct hv_contract_snapshot launch_memory;
static bool launch_memory_valid;
extern u64 hv_ipa_to_pa(u64 ipa);

static unsigned char allocate_page(void *context, APPLE_AGX_UAT_PAGE *page)
{
    void *memory;
    (void)context;
    memory = memalign(0x4000, 0x4000);
    if (!memory) return 0;
    memset(memory, 0, 0x4000);
    page->Entries = memory;
    page->PhysicalAddress = (u64)memory;
    return 1;
}

static void release_page(void *context, const APPLE_AGX_UAT_PAGE *page)
{
    (void)context;
    free(page->Entries);
}

static unsigned long long translate_guest(void *context, unsigned long long ipa)
{
    u64 pa, offset, base = cur_boot_args.phys_base, length = cur_boot_args.mem_size;
    (void)context;
    if (ipa & 0x3fff) return 0;
    pa = hv_ipa_to_pa(ipa);
    /* Existing HVC guest-normal-RAM contract, with complete leaf validation.
     * SW/MMIO/private carveouts cannot resolve via hv_ipa_to_pa. */
    if (!pa || (pa & 0x3fff) || pa < base || length < 0x4000 ||
        pa - base > length - 0x4000 || pa >= (1ULL << 40) ||
        (pa < root_base + root_length && pa + 0x4000 > root_base))
        return 0;
    if (!launch_memory_valid || !hv_agx_retained_backing_allowed(&launch_memory,pa,0x4000,
            J313_AUTONOMOUS_LAYOUT.ramdisk_base,J313_AUTONOMOUS_LAYOUT.ramdisk_max_size))
        return 0;
    for (offset = 0; offset < 0x4000; offset += 0x1000)
        if (hv_ipa_to_pa(ipa + offset) != pa + offset ||
            hv_ipa_to_pa(ipa + offset + 0xfff) != pa + offset + 0xfff)
            return 0;
    return pa;
}

static void sync_tables(void *context)
{
    (void)context;
    /* Native UAT.flush_dirty uses context0 ASIDE1OS; order table stores before
     * invalidation and completion before mailbox grant or releasing storage. */
    /* ASIDE1OS encoding: SYS op1=0 CRn=8 CRm=1 op2=2 (Arm DDI0601).
     * Spell SYS because the pinned global -march=armv8.2-a assembler does not
     * name this v8.4 instruction; native EXP470 executed it on this J313. */
    __asm__ volatile("dsb oshst\n\tsys #0, c8, c1, #2, %0\n\tdsb osh\n\tisb"
                     :: "r"(0ULL) : "memory");
}

static bool cpu_stopped(void)
{
    return asc_base && !(read32(asc_base + 0x44) & BIT(4));
}

static void execute(void *context, const AGX_RR_REQUEST *q, AGX_RR_RESPONSE *r)
{
    static const struct hv_agx_retained_ops ops = {
        NULL, allocate_page, release_page, translate_guest, sync_tables};
    int status = HV_AGX_RETAINED_STATE;
    (void)context;
    if (!root_base || !request_powered) goto done;
    switch (q->Command) {
    case AGX_RR_PREPARE:
        if (q->Epoch || q->Va || q->Ipa || q->Length || q->Handle ||
            !cpu_stopped() || next_epoch == ~0ULL) break;
        status = hv_agx_retained_prepare(&root_owner, root_base, (void *)root_base,
                                         root_length, ++next_epoch, &ops);
        break;
    case AGX_RR_ACTIVATE:
        if (q->Epoch != root_owner.Epoch || cpu_stopped() ||
            q->Va || q->Ipa || q->Length || q->Handle) break;
        status = hv_agx_retained_activate(&root_owner);
        if (!status) {
            write64(HV_AGX_G2_GPU_BASE, root_owner.Roots.Ttbr0PhysicalAddress | 1ULL);
            write64(HV_AGX_G2_GPU_BASE + 8, root_base | 1ULL);
            sync_tables(NULL);
            printf("HV: retained root ACTIVE root=0x%lx prefix=%llx/%llx epoch=%llu\n",
                   root_base, root_owner.PrivatePrefix[0], root_owner.PrivatePrefix[1],
                   root_owner.Epoch);
        }
        break;
    case AGX_RR_MAP:
        if (q->Handle) break;
        status = hv_agx_retained_map(&root_owner,q->Epoch,q->Va,q->Ipa,q->Length,&r->Handle);
        break;
    case AGX_RR_QUERY:
        status = hv_agx_retained_query(&root_owner,q->Epoch,q->Handle,q->Va,q->Ipa,
                                       q->Length,&r->Pa);
        r->Handle = q->Handle;
        break;
    case AGX_RR_UNMAP:
        status = hv_agx_retained_unmap(&root_owner,q->Epoch,q->Handle,q->Va,q->Ipa,q->Length);
        break;
    case AGX_RR_VERIFY_ABSENT:
        status = hv_agx_retained_verify_absent(&root_owner,q->Epoch,q->Handle,q->Va,q->Ipa,q->Length);
        r->Handle = q->Handle;
        break;
    case AGX_RR_CLOSE:
        if (!q->Epoch || q->Epoch != root_owner.Epoch || q->Va || q->Ipa || q->Length || q->Handle ||
            !cpu_stopped()) break;
        /* Root1 retains physical identity. Retire owned root0 before freeing. */
        if (root_owner.Prepared) {
            write64(HV_AGX_G2_GPU_BASE, 0);
            sync_tables(NULL);
        }
        status = hv_agx_retained_close(&root_owner,q->Epoch,1);
        break;
    }
done:
    if (root_owner.Active &&
        (read64(HV_AGX_G2_GPU_BASE + 8) != (root_base | 1ULL) ||
         read64(HV_AGX_G2_GPU_BASE) != (root_owner.Roots.Ttbr0PhysicalAddress | 1ULL))) {
        root_owner.Tainted = 1;
        status = HV_AGX_RETAINED_TAINTED;
    }
    r->Status = status;
    r->Epoch = root_owner.Epoch;
    r->Root = root_owner.Active ? (read64(HV_AGX_G2_GPU_BASE + 8) & 0xffffffc000ULL)
                               : root_base;
    r->Ttbr0 = root_owner.Roots.Ttbr0PhysicalAddress;
    r->SystemVa = root_owner.SystemVa;
    r->SystemBytes = root_owner.SystemBytes;
    r->Count = root_owner.MappingCount;
    r->Flags = (root_owner.Prepared ? AGX_RR_FLAG_PREPARED : 0) |
               (root_owner.Active ? AGX_RR_FLAG_ACTIVE : 0) |
               (hv_agx_retained_prefix_unchanged(&root_owner) ? AGX_RR_FLAG_PREFIX_UNCHANGED : 0);
    printf("HV: retained op=%u seq=%llu status=%u root=0x%lx epoch=%llu va=%llx count=%u flags=%x\n",
           q->Command,q->Sequence,r->Status,root_base,r->Epoch,q->Va,root_owner.MappingCount,r->Flags);
}

bool hv_agx_retained_platform_init(u64 root, u64 length)
{
    int path[8];
    int node = adt_path_offset_trace(adt, "/arm-io/gfx-asc", path);
    u64 size;
    if (node < 0 || adt_get_reg(adt,path,"reg",0,&asc_base,&size) < 0 || size < 0x48)
        return false;
    root_base = root; root_length = length;
    launch_memory_valid = hv_launch_j313_capture_base(HV_CONTRACT_PRE_HV_INIT,1,&launch_memory) &&
        launch_memory.boot.ram_base == J313_AUTONOMOUS_LAYOUT.phys_base;
    if (!launch_memory_valid) return false;
    printf("HV: retained backing guest RAM=0x%llx..0x%llx reserved regions=%u\n",
           (unsigned long long)launch_memory.boot.ram_base,
           (unsigned long long)(launch_memory.boot.ram_base+launch_memory.boot.ram_size),
           launch_memory.region_count);
    return root_base != 0;
}

bool hv_agx_retained_platform_mmio(u64 offset, u64 *value, bool write,
                                  unsigned width, bool powered)
{
    unsigned long long data = *value;
    request_powered = powered;
    bool result = hv_agx_retained_mmio(&wire,offset,&data,write,width,execute,NULL);
    if (result && !write) *value = data;
    return result;
}

bool hv_agx_retained_gpu_region(struct exc_info *ctx, u64 addr, u64 *value,
                                bool write, int width)
{
    u64 offset = addr - HV_AGX_G2_GPU_BASE;
    size_t bytes;
    (void)ctx;
    if (width < 0 || width > 3) return false;
    bytes = 1u << width;
    if (offset > HV_AGX_G2_GPU_SIZE - bytes || (offset & (bytes - 1))) return false;
    if (write && !AgxRrGpuRegionWritable(offset,bytes)) return false;
    if (write) memcpy((void *)addr,value,bytes);
    else { *value = 0; memcpy(value,(void *)addr,bytes); }
    dma_mb();
    return true;
}

bool hv_agx_retained_can_power_off(void)
{
    return !root_owner.Prepared && !root_owner.Active;
}
