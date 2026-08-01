/* SPDX-License-Identifier: MIT */

//
// Emulated NVMe controller behind BAR0 (armed by hv_pci.c once the guest programs the BAR).
// Layer 2 here: the controller register file (CAP/VS/CC/CSTS/AQA/ASQ/ACQ) and the doorbell
// window. CC.EN 0->1 latches the admin-queue geometry and raises CSTS.RDY - that is the gate
// stornvme/NvmExpressDxe waits on before it touches the admin queue. Actual SQE processing,
// CQE completion + phase tag, and INTx are Layer 3 (nvme_admin_doorbell below is the seam).
//
// Guest queue memory (ASQ/ACQ and later I/O queues, PRP pages) is guest-physical and, because
// m1n1 identity-maps guest RAM in stage-2, directly dereferenceable from EL2 - same primitive
// hv_virtio.c uses.
//

#include "hv.h"
#include "string.h"
#include "types.h"
#include "utils.h"

// BAR0 register offsets (NVMe 1.3).
#define NVME_CAP   0x00 // 64-bit, RO
#define NVME_VS    0x08 // 32-bit, RO
#define NVME_INTMS 0x0c // 32-bit, RW1S
#define NVME_INTMC 0x10 // 32-bit, RW1C
#define NVME_CC    0x14 // 32-bit, RW
#define NVME_CSTS  0x1c // 32-bit, RO to guest
#define NVME_AQA   0x24 // 32-bit, RW
#define NVME_ASQ   0x28 // 64-bit, RW
#define NVME_ACQ   0x30 // 64-bit, RW
#define NVME_DBS   0x1000 // doorbell base; stride 4 (CAP.DSTRD=0)

#define CC_EN      BIT(0)
#define CC_SHN_SHIFT 14
#define CC_SHN_MASK  (3u << CC_SHN_SHIFT)

#define CSTS_RDY   BIT(0)
#define CSTS_CFS   BIT(1)
#define CSTS_SHST_SHIFT 2
#define CSTS_SHST_NORMAL   (0u << CSTS_SHST_SHIFT)
#define CSTS_SHST_COMPLETE (2u << CSTS_SHST_SHIFT)

// CAP: MQES=63 (64 entries), CQR=1, TO=10 (5s), DSTRD=0, CSS bit0=NVM command set, MPS 0.
#define NVME_CAP_VALUE                                                                             \
    (0x3fULL | BIT(16) | (10ULL << 24) | (1ULL << 37))
#define NVME_VS_VALUE 0x00010300 // 1.3.0

// Provided by hv_pci.c.
extern int hv_pci_intx_irq(void);

static u64 bar_base;

static struct {
    u32 intms;
    u32 intmc;
    u32 cc;
    u32 csts;
    u32 aqa;
    u64 asq;
    u64 acq;
    u32 sq0tail, cq0head; // admin queue doorbell shadows
} nvme;

// Layer-3 seam: called on an admin submission-queue doorbell write with the new tail.
static void nvme_admin_doorbell(u32 new_tail)
{
    // TODO(Layer 3): walk ASQ from old head to new_tail, fetch 64-byte SQEs from guest RAM,
    // execute IDENTIFY / SET_FEATURES / CREATE_IO_*Q / GET_LOG_PAGE (park ASYNC_EVENT), post
    // CQEs to ACQ with the phase tag, and assert INTx via aic_set_sw(hv_pci_intx_irq(), true).
    printf("HV: NVMe admin SQ0 doorbell -> tail %u (admin processing: Layer 3)\n", new_tail);
    nvme.sq0tail = new_tail;
}

static u64 reg_read(u32 off, int width)
{
    u64 v;
    switch (off & ~7u) {
        case NVME_CAP:
            v = NVME_CAP_VALUE;
            break;
        case NVME_ASQ:
            v = nvme.asq;
            break;
        case NVME_ACQ:
            v = nvme.acq;
            break;
        default:
            // 32-bit register file
            switch (off & ~3u) {
                case NVME_VS:
                    v = NVME_VS_VALUE;
                    break;
                case NVME_INTMS:
                    v = nvme.intms;
                    break;
                case NVME_INTMC:
                    v = nvme.intmc;
                    break;
                case NVME_CC:
                    v = nvme.cc;
                    break;
                case NVME_CSTS:
                    v = nvme.csts;
                    break;
                case NVME_AQA:
                    v = nvme.aqa;
                    break;
                default:
                    v = 0;
                    break;
            }
            // shift into place if this is the high dword of an 8-byte-aligned slot
            if (off & 4)
                v <<= 32;
            break;
    }
    // extract the accessed sub-field
    u32 boff = off & 7;
    u32 bytes = 1u << width;
    if (bytes >= 8)
        return v;
    u64 mask = (1ULL << (bytes * 8)) - 1;
    return (v >> (boff * 8)) & mask;
}

static void cc_write(u32 val)
{
    u32 old = nvme.cc;
    nvme.cc = val;

    if ((val & CC_EN) && !(old & CC_EN)) {
        // Enable: admin queue geometry in AQA/ASQ/ACQ is now valid; come ready.
        printf("HV: NVMe CC.EN 0->1 (AQA=0x%x ASQ=0x%lx ACQ=0x%lx), CSTS.RDY=1\n",
               nvme.aqa, nvme.asq, nvme.acq);
        nvme.csts |= CSTS_RDY;
    } else if (!(val & CC_EN) && (old & CC_EN)) {
        nvme.csts &= ~CSTS_RDY;
    }

    if (val & CC_SHN_MASK) {
        // Clean shutdown requested.
        nvme.csts = (nvme.csts & ~(3u << CSTS_SHST_SHIFT)) | CSTS_SHST_COMPLETE;
    }
}

static void reg_write(u32 off, int width, u64 val)
{
    switch (off & ~7u) {
        case NVME_ASQ:
            if (width >= 3)
                nvme.asq = val;
            else if (off & 4)
                nvme.asq = (nvme.asq & 0xffffffffULL) | (val << 32);
            else
                nvme.asq = (nvme.asq & ~0xffffffffULL) | (u32)val;
            return;
        case NVME_ACQ:
            if (width >= 3)
                nvme.acq = val;
            else if (off & 4)
                nvme.acq = (nvme.acq & 0xffffffffULL) | (val << 32);
            else
                nvme.acq = (nvme.acq & ~0xffffffffULL) | (u32)val;
            return;
        default:
            break;
    }
    switch (off & ~3u) {
        case NVME_INTMS:
            nvme.intms |= (u32)val; // mask INTx
            break;
        case NVME_INTMC:
            nvme.intms &= ~(u32)val; // unmask INTx
            break;
        case NVME_CC:
            cc_write((u32)val);
            break;
        case NVME_AQA:
            nvme.aqa = (u32)val;
            break;
        default:
            break; // CAP/VS/CSTS read-only; drop
    }
}

// Stage-2 hook over the 16 MB BAR0 region.
static bool handle_nvme_bar(struct exc_info *ctx, u64 addr, u64 *val, bool write, int width)
{
    UNUSED(ctx);
    u32 off = addr - bar_base;

    if (off >= NVME_DBS) {
        // Doorbell window: stride 4. Even index = SQ tail, odd = CQ head; queue = index/2.
        u32 idx = (off - NVME_DBS) / 4;
        if (write) {
            if (idx == 0)
                nvme_admin_doorbell((u32)*val); // SQ0 tail (admin)
            else if (idx == 1)
                nvme.cq0head = (u32)*val; // CQ0 head (admin) - INTx deassert point (Layer 3)
            else
                printf("HV: NVMe doorbell idx %u <- %u (I/O queue: Layer 3)\n", idx, (u32)*val);
        } else {
            *val = 0; // doorbells are write-only
        }
        return true;
    }

    if (write)
        reg_write(off, width, *val);
    else
        *val = reg_read(off, width);
    return true;
}

void hv_nvme_map_bar(u64 base)
{
    bar_base = base;
    memset(&nvme, 0, sizeof(nvme));
    hv_map_hook(base, handle_nvme_bar, 0x1000000); // 16 MB
    printf("HV: NVMe BAR0 MMIO live at 0x%lx\n", base);
}

void hv_nvme_unmap_bar(void)
{
    if (bar_base) {
        hv_unmap(bar_base, 0x1000000);
        bar_base = 0;
    }
}
