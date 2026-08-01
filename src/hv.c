/* SPDX-License-Identifier: MIT */

#include "hv.h"
#include "assert.h"
#include "cpu_regs.h"
#include "display.h"
#include "gxf.h"
#include "hv_fb_stream.h"
#include "memory.h"
#include "pcie.h"
#include "smp.h"
#include "string.h"
#include "usb.h"
#include "uartproxy.h"
#include "utils.h"
#include "adt.h"
#include "xnuboot.h"

#define HV_TICK_RATE      5000
#define HV_SLOW_TICK_RATE 1

DECLARE_SPINLOCK(bhl);

void hv_enter_guest(u64 x0, u64 x1, u64 x2, u64 x3, void *entry);
void hv_exit_guest(void) __attribute__((noreturn));

extern char _hv_vectors_start[0];

u64 hv_tick_interval;
u64 hv_secondary_tick_interval;

int hv_pinned_cpu;
int hv_want_cpu;

static bool hv_has_ecv;
static bool hv_should_exit[MAX_CPUS];
bool hv_started_cpus[MAX_CPUS];
u64 hv_cpus_in_guest;
u64 hv_saved_sp[MAX_CPUS];
static struct hv_fb_stream hv_framebuffer_stream;

static bool hv_send_fb_chunk(void *opaque, const struct hv_fb_chunk_header *header,
                             const void *payload)
{
    (void)opaque;
    return uartproxy_try_send_eventv(EVT_FRAMEBUFFER, header, sizeof(*header), payload,
                                     header->payload_size);
}

bool hv_configure_fb_stream(u64 ipa, u64 size, u64 width, u64 height, u64 stride)
{
    if (!ipa && !size && !width && !height && !stride) {
        hv_fb_stream_disable(&hv_framebuffer_stream);
        return true;
    }
    if (width > 0xffffffffULL || height > 0xffffffffULL || stride > 0xffffffffULL)
        return false;

    return hv_fb_stream_configure(&hv_framebuffer_stream, ipa, size, width, height, stride,
                                  hv_ipa_to_pa, hv_send_fb_chunk, NULL);
}

struct hv_secondary_info_t {
    uint64_t hcr;
    uint64_t hacr;
    uint64_t vtcr, vttbr;
    uint64_t mdcr;
    uint64_t mdscr;
    uint64_t amx_ctl;
    uint64_t apvmkeylo, apvmkeyhi, apsts;
    uint64_t actlr_el2;
    uint64_t actlr_el1;
    uint64_t cnthctl;
    uint64_t sprr_config;
    uint64_t gxf_config;
};

static struct hv_secondary_info_t hv_secondary_info;

void hv_init(void)
{
    hv_fb_stream_disable(&hv_framebuffer_stream);
    pcie_shutdown();
    // Make sure we wake up DCP if we put it to sleep, just quiesce it to match ADT
    if (display_is_external && display_start_dcp() >= 0)
        display_shutdown(DCP_QUIESCED);
    // reenable hpm interrupts for the guest for unused iodevs
    usb_hpm_restore_irqs(0);
    smp_start_secondaries();
    smp_set_wfe_mode(true);
    hv_wdt_init();

    hv_pt_init();

    // Configure hypervisor defaults

    //
    // UNKNOWN: do we need to bring TGE back? might have misunderstood why it was there at the start.
    // leaving it off for now.
    //
    hv_write_hcr(HCR_API | // Allow PAuth instructions
                 HCR_APK | // Allow PAuth key registers
                 HCR_TEA | // Trap external aborts
                 HCR_RW |  // AArch64 guest
                 HCR_TSC | // Trap SMC exceptions (only writable on Blizzard/Avalanche cores as the previous generations used a chicken bit for this.)
                 HCR_AMO | // Trap SError exceptions
                 HCR_IMO | // Trap IRQ exceptions (for now)
                 HCR_FMO | // Trap FIQ exceptions (effectively required for now)
#ifdef ENABLE_VGIC_MODULE
                 //
                 // Trap EL1 reads of the ID registers. Without this the "advertise GIC"
                 // case for ID_AA64PFR0_EL1 in hv_exc.c never runs: the guest reads the
                 // real register, sees GIC = 0 (the M1 implements the GIC system
                 // registers but does not advertise them), and both EDK2 and the Windows
                 // HAL fall back to driving a GICv2 over MMIO -- for which there is no
                 // delivery path. hv_handle_msr_unlocked() serves the whole ID space.
                 //
                 // Note: this must live here and not in exception_initialize(), because
                 // this call rewrites HCR_EL2 wholesale and would drop the bit.
                 //
                 HCR_TID3 | // Trap ID register reads from EL1
#endif
                 HCR_VM);  // Enable stage 2 translation

    // No guest vectors initially
    msr(VBAR_EL12, 0);

    //set up a HACR bit (56)
    printf("DEBUG: setting up HACR\n");
    uint64_t hacr_val = mrs(HACR_EL2);
    hacr_val |= BIT(56);
    msr(HACR_EL2, hacr_val);

    //
    // m1n1_windows change: initialize PSCI.
    //
    printf("DEBUG: setting up PSCI\n");
    hv_psci_init();
#ifdef ENABLE_VGIC_MODULE
    //
    // m1n1_windows change: set up the vGIC
    //

    //
    hv_vgicv3_init();
    init_vgic_irq_queues();
    //
#endif

    // Compute tick interval
    hv_tick_interval = mrs(CNTFRQ_EL0) / HV_TICK_RATE;

    hv_has_ecv = mrs(ID_AA64MMFR0_EL1) & (0xfULL << 60);

    if (hv_has_ecv) {
        printf("HV: ECV enabled\n");
        reg_set(CNTHCTL_EL2,
                CNTHCTL_EL1NVVCT | CNTHCTL_EL1NVPCT | CNTHCTL_EL1TVT | CNTHCTL_EL1PCTEN);
        hv_secondary_tick_interval = mrs(CNTFRQ_EL0) / HV_SLOW_TICK_RATE;
    } else {
        printf("HV: No ECV supported\n");
        // Enable physical timer for EL1
        msr(CNTHCTL_EL2, CNTHCTL_EL1PTEN | CNTHCTL_EL1PCTEN);

        hv_secondary_tick_interval = hv_tick_interval;
    }

    // Set deep WFI back to defaults
    if (cpu_features->cyc_ovrd)
        reg_mask(SYS_IMP_APL_CYC_OVRD, CYC_OVRD_WFI_MODE_MASK, CYC_OVRD_WFI_MODE(0));

    sysop("dsb ishst");
    sysop("tlbi alle1is");
    sysop("dsb ish");
    sysop("isb");
}

static void hv_set_gxf_vbar(void)
{
    msr(SYS_IMP_APL_VBAR_GL1, _hv_vectors_start);
}

void hv_start(void *entry, u64 regs[4])
{
    if (boot_cpu_idx == -1) {
        printf("Boot CPU has not been found, can't start hypervisor\n");
        return;
    }

    memset(hv_should_exit, 0, sizeof(hv_should_exit));
    memset(hv_started_cpus, 0, sizeof(hv_started_cpus));

    hv_started_cpus[boot_cpu_idx] = true;

    msr(VBAR_EL1, _hv_vectors_start);

    if (gxf_enabled())
        gl2_call(hv_set_gxf_vbar, 0, 0, 0, 0);

    hv_secondary_info.hcr = mrs(HCR_EL2);
    hv_secondary_info.hacr = mrs(HACR_EL2);
    hv_secondary_info.vtcr = mrs(VTCR_EL2);
    hv_secondary_info.vttbr = mrs(VTTBR_EL2);
    hv_secondary_info.mdcr = mrs(MDCR_EL2);
    hv_secondary_info.mdscr = mrs(MDSCR_EL1);
    hv_secondary_info.amx_ctl = mrs(SYS_IMP_APL_AMX_CTL_EL2);
    hv_secondary_info.apvmkeylo = mrs(SYS_IMP_APL_APVMKEYLO_EL2);
    hv_secondary_info.apvmkeyhi = mrs(SYS_IMP_APL_APVMKEYHI_EL2);
    hv_secondary_info.apsts = mrs(SYS_IMP_APL_APSTS_EL12);
    hv_secondary_info.actlr_el2 = mrs(ACTLR_EL2);
    if (cpu_features->actlr_el2)
        hv_secondary_info.actlr_el1 = mrs(SYS_ACTLR_EL12);
    else
        hv_secondary_info.actlr_el1 = mrs(SYS_IMP_APL_ACTLR_EL12);
    hv_secondary_info.cnthctl = mrs(CNTHCTL_EL2);
    hv_secondary_info.sprr_config = mrs(SYS_IMP_APL_SPRR_CONFIG_EL1);
    hv_secondary_info.gxf_config = mrs(SYS_IMP_APL_GXF_CONFIG_EL1);

#ifdef ENABLE_VGIC_MODULE
    hv_vgicv3_enable_virtual_interrupts();
    hv_vgicv3_init_list_registers();
#endif

    hv_arm_tick(false);
    hv_pinned_cpu = -1;
    hv_want_cpu = -1;
    hv_cpus_in_guest = BIT(smp_id());

    u64 adt_base;
    if(chip_id == T8103 || chip_id == T8112)
        adt_base = ADT_EL2_36_BIT;
    else
        adt_base = ADT_EL2_42_BIT;

    //map the address of the (EL2) ADT to a fixed location so EL1 can patch it
    hv_map_hw(adt_base, (u64)adt, ALIGN_UP(cur_boot_args.devtree_size, SZ_16K));

    hv_enter_guest(regs[0], regs[1], regs[2], regs[3], entry);

    __atomic_and_fetch(&hv_cpus_in_guest, ~BIT(smp_id()), __ATOMIC_ACQUIRE);
    spin_lock(&bhl);

    hv_wdt_stop();

    printf("HV: Exiting hypervisor (main CPU)\n");

    spin_unlock(&bhl);
    // Wait a bit for the guest CPUs to exit on their own if they are in the process.
    udelay(200000);
    spin_lock(&bhl);

    hv_started_cpus[boot_cpu_idx] = false;

    for (int i = 0; i < MAX_CPUS; i++) {
        if (i == boot_cpu_idx) {
            continue;
        }
        hv_should_exit[i] = true;
        if (hv_started_cpus[i]) {
            printf("HV: Waiting for CPU %d to exit\n", i);
            spin_unlock(&bhl);
            smp_wait(i);
            spin_lock(&bhl);
            hv_started_cpus[i] = false;
        }
    }

    printf("HV: All CPUs exited\n");
    spin_unlock(&bhl);
}

static void hv_init_secondary(struct hv_secondary_info_t *info)
{
    gxf_init();

    msr(VBAR_EL1, _hv_vectors_start);

    msr(HCR_EL2, info->hcr);
    msr(HACR_EL2, info->hacr);
    msr(VTCR_EL2, info->vtcr);
    msr(VTTBR_EL2, info->vttbr);
    msr(MDCR_EL2, info->mdcr);
    msr(MDSCR_EL1, info->mdscr);
    msr(SYS_IMP_APL_AMX_CTL_EL2, info->amx_ctl);
    msr(SYS_IMP_APL_APVMKEYLO_EL2, info->apvmkeylo);
    msr(SYS_IMP_APL_APVMKEYHI_EL2, info->apvmkeyhi);
    msr(SYS_IMP_APL_APSTS_EL12, info->apsts);
    msr(ACTLR_EL2, info->actlr_el2);
    if (cpu_features->actlr_el2)
        msr(SYS_ACTLR_EL12, info->actlr_el1);
    else
        msr(SYS_IMP_APL_ACTLR_EL12, info->actlr_el1);
    msr(CNTHCTL_EL2, info->cnthctl);
    msr(SYS_IMP_APL_SPRR_CONFIG_EL1, info->sprr_config);
    msr(SYS_IMP_APL_GXF_CONFIG_EL1, info->gxf_config);

#ifdef ENABLE_VGIC_MODULE
    hv_vgicv3_enable_virtual_interrupts();
    hv_vgicv3_init_list_registers();
#endif

    if (cpu_features->cyc_ovrd)
        reg_mask(SYS_IMP_APL_CYC_OVRD, CYC_OVRD_WFI_MODE_MASK, CYC_OVRD_WFI_MODE(0));

    if (gxf_enabled())
        gl2_call(hv_set_gxf_vbar, 0, 0, 0, 0);

    hv_arm_tick(true);
}

static void hv_enter_secondary(void *entry, u64 regs[4])
{
    hv_enter_guest(regs[0], regs[1], regs[2], regs[3], entry);

    spin_lock(&bhl);

    printf("HV: Exiting from CPU %d\n", smp_id());

    __atomic_and_fetch(&hv_cpus_in_guest, ~BIT(smp_id()), __ATOMIC_ACQUIRE);

    hv_started_cpus[smp_id()] = false;
    spin_unlock(&bhl);
}

void hv_start_secondary(int cpu, void *entry, u64 regs[4])
{
    printf("HV: Initializing secondary %d\n", cpu);
    iodev_console_flush();

    mmu_init_secondary(cpu);
    iodev_console_flush();
    smp_call4(cpu, hv_init_secondary, (u64)&hv_secondary_info, 0, 0, 0);
    smp_wait(cpu);
    iodev_console_flush();

    printf("HV: Entering guest secondary %d at %p\n", cpu, entry);
    hv_started_cpus[cpu] = true;
    __atomic_or_fetch(&hv_cpus_in_guest, BIT(smp_id()), __ATOMIC_ACQUIRE);

    iodev_console_flush();
    smp_call4(cpu, hv_enter_secondary, (u64)entry, (u64)regs, 0, 0);
}

void hv_exit_cpu(int cpu)
{
    if (cpu == -1)
        cpu = smp_id();

    printf("HV: Requesting exit of CPU#%d from the guest\n", cpu);
    hv_should_exit[cpu] = true;
}

void hv_rendezvous(void)
{
    int timeout = 1000000;

    if (!__atomic_load_n(&hv_cpus_in_guest, __ATOMIC_ACQUIRE))
        return;

    /* IPI all CPUs. This might result in spurious IPIs to the guest... */
    for (int i = 0; i < MAX_CPUS; i++) {
        if (i != smp_id() && hv_started_cpus[i]) {
            smp_send_ipi(i);
        }
    }

    while (timeout--) {
        if (!__atomic_load_n(&hv_cpus_in_guest, __ATOMIC_ACQUIRE))
            return;
    }

    hv_panic("HV: Failed to rendezvous, missing CPUs: 0x%lx (current: %d)\n",
             __atomic_load_n(&hv_cpus_in_guest, __ATOMIC_ACQUIRE), smp_id());
}

bool hv_switch_cpu(int cpu)
{
    if (cpu > MAX_CPUS || cpu < 0 || !hv_started_cpus[cpu]) {
        printf("HV: CPU #%d is inactive or invalid\n", cpu);
        return false;
    }
    printf("HV: switching to CPU #%d\n", cpu);
    hv_want_cpu = cpu;
    hv_rendezvous();
    return true;
}

void hv_pin_cpu(int cpu)
{
    hv_pinned_cpu = cpu;
}

void hv_write_hcr(u64 val)
{
    if (gxf_enabled() && !in_gl12())
        gl2_call(hv_write_hcr, val, 0, 0, 0);
    else
        msr(HCR_EL2, val);
}

u64 hv_get_spsr(void)
{
    if (in_gl12())
        return mrs(SYS_IMP_APL_SPSR_GL1);
    else
        return mrs(SPSR_EL2);
}

void hv_set_spsr(u64 val)
{
    if (in_gl12())
        return msr(SYS_IMP_APL_SPSR_GL1, val);
    else
        return msr(SPSR_EL2, val);
}

u64 hv_get_esr(void)
{
    if (in_gl12())
        return mrs(SYS_IMP_APL_ESR_GL1);
    else
        return mrs(ESR_EL2);
}

u64 hv_get_far(void)
{
    if (in_gl12())
        return mrs(SYS_IMP_APL_FAR_GL1);
    else
        return mrs(FAR_EL2);
}

u64 hv_get_afsr1(void)
{
    if (in_gl12())
        return mrs(SYS_IMP_APL_AFSR1_GL1);
    else
        return mrs(AFSR1_EL2);
}

u64 hv_get_elr(void)
{
    if (in_gl12())
        return mrs(SYS_IMP_APL_ELR_GL1);
    else
        return mrs(ELR_EL2);
}

void hv_set_elr(u64 val)
{
    if (in_gl12())
        return msr(SYS_IMP_APL_ELR_GL1, val);
    else
        return msr(ELR_EL2, val);
}

void hv_arm_tick(bool secondary)
{
    if (secondary)
        msr(CNTP_TVAL_EL0, hv_secondary_tick_interval);
    else
        msr(CNTP_TVAL_EL0, hv_tick_interval);
    msr(CNTP_CTL_EL0, CNTx_CTL_ENABLE);
}

void hv_maybe_exit(void)
{
    if (hv_should_exit[smp_id()]) {
        hv_exit_guest();
    }
}

//
// Sample where the guest is, from the hypervisor's own timer, and report a stop.
//
// Everything here obeys one hard rule, learned four times over: the FIQ handler runs on a
// small stack and a tight budget, and *any* extra work inside it kills the entire FIQ path.
// The failure is deceptive - PC samples and the heartbeat vanish together while
// synchronous-exception handling carries on, so it looks like an unrelated breakage.
//
// So the handler only ever copies. Collection is a fixed set of loads into static storage;
// printing is one line per tick, from that storage, long after the fact.
//
// This diagnostic walks mutable guest stacks from the timer FIQ. Keep it opt-in:
// a transient Windows spinlock is not a crash, and an unlucky cross-page diagnostic
// read must never take down the timer/event path used by the guest and framebuffer.
bool hv_pc_sampling = false;

#define STUCK_FRAMES 12
#define STUCK_CODE   12
#define STUCK_LINES  (4 + STUCK_CODE + STUCK_FRAMES + 1 + 1 + 1 + STUCK_CODE)

static struct {
    u64 pc, pa, spsr, sp;
    u64 regs[31];
    u32 code[STUCK_CODE];
    u64 frame_fp[STUCK_FRAMES];
    u64 frame_lr[STUCK_FRAMES];
    u64 frame_code[STUCK_FRAMES];   // the instruction that made the call
    u64 octx[4];        // Fp, Lr, Sp, Pc of the CONTEXT KiDispatchException built
    bool octx_ok;
    u64 far;            // guest FAR_EL1 at collect time; brk does not overwrite it, so
                        // it should still hold the original abort's faulting address
    //
    // Rather than guess where the KTRAP_FRAME is, scan the abort-dispatch stack for the
    // faulting address itself: KTRAP_FRAME.Pc == far for an instruction abort, so every
    // slot holding far is a candidate, and the words on either side are Lr/Fp/Sp. No
    // offset is trusted - the match locates the frame.
    //
    //
    // The abort's faulting instruction, resolved through the KTRAP_FRAME:
    // trap_ptr = [frame_fp[7]+0x28] (KiAbortException's saved x20 = its trap-frame arg),
    // fault_pc = [trap_ptr+0x148], and [trap_ptr+0x90] is the abort ESR (a self-check).
    // Offsets are from KiAbortException's disassembly, not guessed.
    //
    u64 trap_ptr;
    u64 fault_pc;
    u64 tf_esr;
    u64 fpc_pa;
    u32 fpc_code[STUCK_CODE];
    int fpc_read;
    int fpc_state;
    u64 bcd[5];         // KiBugCheckData: code + params 1..4, from x19
    u64 bcd_va;
    bool bcd_ok;
    u64 stack[32];      // filled a few words per tick, see hv_read_stack_slice()
    u64 stack_pa;
    u64 stack_base;
    int stack_read;
    //
    // Two EXCEPTION_RECORDs, one per pass through KiSynchronousException: index 0 is the
    // inner one (the bugcheck path's own debug break), index 1 the outer one - the
    // original abort that started it all. Same geometry both times: the record lives at
    // that pass's frame fp + 0x18.
    //
    u64 rec_base[2];
    u64 rec_pa[2];
    u64 rec[2][8];
    int rec_read[2];
    u64 exc_addr[2];    // ExceptionAddress out of each record
    u64 exc_pa[2];
    u32 exc_code[2][STUCK_CODE];
    int exc_read[2];
    int frames;
    int printed;
    bool valid;
} stuck;

static void __attribute__((noinline)) hv_collect_stuck(struct exc_info *ctx)
{
    u64 pa = hv_translate(ctx->elr & ~3UL, false, false, NULL);

    stuck.pc = ctx->elr;
    stuck.pa = pa;
    stuck.spsr = ctx->spsr;
    stuck.sp = ctx->sp[1];

    for (int i = 0; i < 31; i++)
        stuck.regs[i] = ctx->regs[i];

    for (int i = 0; i < STUCK_CODE; i++)
        stuck.code[i] = pa ? read32(pa + (i - 4) * 4) : 0;

    //
    // The kernel image is physically contiguous, so every address inside it is reachable
    // by arithmetic from the stop point. That matters: one hv_translate per frame is
    // affordable here, two was not.
    //
    u64 off = pa ? pa - (ctx->elr & ~3UL) : 0;
    u64 fp = ctx->regs[29];

    //
    // The frames all live on the same kernel stack, so cache the page translation:
    // twelve frames cost one to three hv_translate calls instead of twelve. A record
    // straddling a page boundary (fp ending in 0xff8) skips the cache.
    //
    u64 page_va = 1, page_pa = 0;

    stuck.frames = 0;
    for (int depth = 0; depth < STUCK_FRAMES && fp && off; depth++) {
        u64 va = fp & ~7UL;
        u64 fp_pa;
        if (page_pa && (va & ~0xFFFUL) == page_va && (va & 0xFFF) <= 0xFF0) {
            fp_pa = page_pa + (va & 0xFFF);
        } else {
            fp_pa = hv_translate(va, false, false, NULL);
            if (!fp_pa)
                break;
            page_va = va & ~0xFFFUL;
            page_pa = fp_pa - (va & 0xFFF);
        }

        u64 next_fp = read64(fp_pa);
        u64 lr = read64(fp_pa + 8);

        stuck.frame_fp[depth] = fp;
        stuck.frame_lr[depth] = lr;
        //
        // The instruction two words before the return address: for the frame that called
        // KeBugCheckEx that is the `mov w0, #<code>` setting the bugcheck code.
        //
        // Only if lr is plausibly inside the same physically-contiguous image as the
        // stop pc: the pa here comes from arithmetic, not from a translation, and an
        // lr from another module (or a garbage lr slot) sends the read into arbitrary
        // physical space - which faults at EL2 and takes the whole hypervisor down.
        // That is precisely how the vUART kept dying seconds after the guest stopped.
        //
        stuck.frame_code[depth] =
            (lr - stuck.pc + 0x800000UL) < 0x1000000UL ? read32(lr + off - 8) : 0;
        stuck.frames = depth + 1;

        if (next_fp <= fp)
            break;
        fp = next_fp;
    }

    //
    // Window the stack on frame 1 rather than on the stop's own SP.
    //
    // Frame 1 is the function that called KeBugCheckEx - the disassembly showed it loading
    // the parameters and setting w0 to 0x1e - so its frame is where those arguments live.
    // The window from SP showed only the breakpoint spin's own frame. Same cost: one
    // translation either way, and the reading is still spread over later ticks.
    //
    stuck.stack_base = stuck.frames > 1 ? stuck.frame_fp[1] - 0x40 : ctx->sp[1];
    stuck.stack_pa = hv_translate(stuck.stack_base & ~7UL, false, false, NULL);
    stuck.stack_read = 0;
    //
    // Frame 3's fp pair is the one KiSynchronousException saved (the lr chain runs one
    // deeper than the fp chain), and its prologue is `stp x29,x30,[sp,#-0xb0]!; mov
    // x29,sp` with the original EXCEPTION_RECORD built at sp+0x18 (`add x0,sp,#0x18`
    // just before `bl KiDispatchException`). Confirmed against the dump: the pointer
    // spilled at window+0x40 equals fp3+0x18.
    //
    stuck.rec_base[0] = stuck.frames > 3 ? stuck.frame_fp[3] + 0x18 : 0;
    stuck.rec_base[1] = stuck.frames > 9 ? stuck.frame_fp[9] + 0x18 : 0;
    for (int r = 0; r < 2; r++) {
        stuck.rec_pa[r] = 0;
        stuck.rec_read[r] = 0;
        stuck.exc_addr[r] = 0;
        stuck.exc_pa[r] = 0;
        stuck.exc_read[r] = 0;
    }
    //
    // Read the CONTEXT words (window+0x140..0x158: X29, X30, Sp, Pc at the moment of
    // the inner exception) here rather than on a later tick: four reads on a page
    // already translated. They have to be in the very first printed line - the vUART
    // link now dies within a tick or two of the guest stopping, so anything essential
    // must go out immediately.
    //
    stuck.octx_ok = stuck.stack_pa && ((stuck.stack_base & 0xFFF) + 0x158) < 0x1000;
    for (int i = 0; i < 4; i++)
        stuck.octx[i] = stuck.octx_ok ? read64(stuck.stack_pa + 0x140 + i * 8) : 0;

    stuck.far = mrs(FAR_EL12);

    //
    // KiBugCheckData: x19 held its address at the KeBugCheckEx call (x19 = nt+0xdbb9a0,
    // confirmed against symbols). Five qwords: BugCheckCode then Parameters 1..4. For
    // bugcheck 0xA that is (referenced address, IRQL, access type, faulting PC) - which
    // settles the 0xA-vs-0xC8 question and says whether the fault happened at raised
    // IRQL. One translation, five reads, done once here.
    //
    stuck.bcd_va = stuck.regs[19] & ~7UL;
    u64 bcd_pa = hv_translate(stuck.bcd_va, false, false, NULL);
    //
    // Once KiDisplayBlueScreen is running, x19 no longer points at KiBugCheckData. This
    // Windows 26100.8037 build spins in HalpGitQueryCounter at nt+0x213a98 while drawing
    // the blue screen; use that uniquely identified PC to recover nt base and the public
    // KiBugCheckData symbol (nt+0xdbb9a0). The image/PDB-specific offsets are already the
    // basis of the stack decoder above, and the opcode check prevents a false match.
    //
    if ((!bcd_pa || !read64(bcd_pa) || read64(bcd_pa) >= 0x1000) &&
        stuck.code[4] == 0xd53be040) {
        stuck.bcd_va = stuck.pc - 0x213a98UL + 0xdbb9a0UL;
        bcd_pa = hv_translate(stuck.bcd_va, false, false, NULL);
    }
    stuck.bcd_ok = bcd_pa != 0;
    for (int i = 0; i < 5; i++)
        stuck.bcd[i] = bcd_pa ? read64(bcd_pa + i * 8) : 0;

    stuck.trap_ptr = 0;
    stuck.fault_pc = 0;
    stuck.tf_esr = 0;
    stuck.fpc_pa = 0;
    stuck.fpc_read = 0;
    stuck.fpc_state = stuck.frames > 7 ? 0 : 9;

    stuck.printed = 0;
    stuck.valid = true;
}

//
// Four words per tick. The measured budget of the FIQ handler is about seven address
// translations and thirty reads; anything beyond that kills the whole FIQ path, silently
// and in a way that looks like an unrelated failure.
//
static void hv_read_stack_slice(void)
{
    if (!stuck.valid || !stuck.stack_pa)
        return;

    if (stuck.stack_read < 32) {
        for (int i = 0; i < 4; i++, stuck.stack_read++)
            stuck.stack[stuck.stack_read] = read64(stuck.stack_pa + stuck.stack_read * 8);
        return;
    }

    //
    // The stack words are in; next comes the original EXCEPTION_RECORD. One translation
    // on its own tick, then four words per tick - the same pacing the stack reads
    // survive. Everything here changes per boot with the kernel base, so it all has to
    // come from the walked frames, not from constants.
    //
    //
    // Each record, then the code around its ExceptionAddress (rec[2]). One phase per
    // tick, one translation per phase at most.
    //
    for (int r = 0; r < 2; r++) {
        if (stuck.rec_base[r] && !stuck.rec_pa[r]) {
            stuck.rec_pa[r] = hv_translate(stuck.rec_base[r] & ~7UL, false, false, NULL);
            if (!stuck.rec_pa[r])
                stuck.rec_base[r] = 0;  // translation failed; skip record and code dump
            return;
        }

        if (stuck.rec_pa[r] && stuck.rec_read[r] < 8) {
            for (int i = 0; i < 4; i++, stuck.rec_read[r]++)
                stuck.rec[r][stuck.rec_read[r]] =
                    read64(stuck.rec_pa[r] + stuck.rec_read[r] * 8);
            return;
        }

        if (!stuck.exc_addr[r]) {
            u64 addr = stuck.rec_base[r] ? stuck.rec[r][2] : 0;
            if ((addr >> 40) != 0xfffff8)   // not a kernel VA; leave exc_pa 0, give up
                addr = ~0UL;
            else
                stuck.exc_pa[r] = hv_translate(addr & ~3UL, false, false, NULL);
            stuck.exc_addr[r] = addr;
            return;
        }

        if (stuck.exc_pa[r] && stuck.exc_read[r] < STUCK_CODE) {
            for (int i = 0; i < 4; i++, stuck.exc_read[r]++)
                stuck.exc_code[r][stuck.exc_read[r]] =
                    read32(stuck.exc_pa[r] + (stuck.exc_read[r] - 4) * 4);
            return;
        }
    }

    //
    // Resolve the faulting instruction through the KTRAP_FRAME, one deref per tick.
    //
    if (stuck.fpc_state == 0) {   // trap_ptr = [frame_fp[7]+0x28]
        u64 pa = hv_translate((stuck.frame_fp[7] + 0x28) & ~7UL, false, false, NULL);
        stuck.trap_ptr = pa ? read64(pa) : 0;
        stuck.fpc_state = ((stuck.trap_ptr >> 40) == 0xfffff8) ? 1 : 9;
        return;
    }

    if (stuck.fpc_state == 1) {   // esr self-check and fault_pc
        u64 ea = hv_translate((stuck.trap_ptr + 0x90) & ~7UL, false, false, NULL);
        stuck.tf_esr = ea ? read64(ea + ((stuck.trap_ptr + 0x90) & 7)) : 0;
        u64 pa = hv_translate((stuck.trap_ptr + 0x148) & ~7UL, false, false, NULL);
        stuck.fault_pc = pa ? read64(pa + ((stuck.trap_ptr + 0x148) & 7)) : 0;
        stuck.fpc_state = ((stuck.fault_pc >> 40) == 0xfffff8) ? 2 : 9;
        return;
    }

    if (stuck.fpc_state == 2) {   // translate fault_pc
        stuck.fpc_pa = hv_translate(stuck.fault_pc & ~3UL, false, false, NULL);
        stuck.fpc_state = stuck.fpc_pa ? 3 : 9;
        return;
    }

    if (stuck.fpc_state == 3 && stuck.fpc_read < STUCK_CODE) {   // code around fault_pc
        for (int i = 0; i < 4; i++, stuck.fpc_read++)
            stuck.fpc_code[stuck.fpc_read] = read32(stuck.fpc_pa + (stuck.fpc_read - 4) * 4);
        if (stuck.fpc_read >= STUCK_CODE)
            stuck.fpc_state = 9;
        return;
    }
}

static void hv_print_stuck_line(void)
{
    int n = stuck.printed;

    if (!stuck.valid || n >= STUCK_LINES)
        return;

    stuck.printed++;

    if (n == 0)
        //
        // Everything essential in one line, printed on the collect tick itself: the
        // vUART link tends to die within a tick or two of the guest stopping. code= is
        // the `mov w0, #<bugcheck>` of the frame that called KeBugCheckEx; olr/opc are
        // Lr and Pc at the moment of the inner exception.
        //
        printf("HV STUCK: pc=0x%lx code=%08x f1lr=0x%lx olr=0x%lx opc=0x%lx far=0x%lx\n",
               stuck.pc, (u32)stuck.frame_code[1], stuck.frame_lr[1], stuck.octx[1],
               stuck.octx[3], stuck.far);
    else if (n == 1) {
        //
        // KiBugCheckData (code + params 1..4) - but x19 only points at it in the frame
        // that called KeBugCheckEx. Anywhere else (e.g. an idle or spinlock stop) x19 is
        // an unrelated register, so bcd[] is garbage. Only print it when x19 is a kernel
        // VA and bcd[0] is a plausible bugcheck code (small, non-zero); else say so.
        //
        if (stuck.bcd_ok && stuck.bcd[0] && stuck.bcd[0] < 0x1000)
            printf("HV STUCK: bugcheck=%lx P1=0x%lx P2=0x%lx P3=0x%lx P4=0x%lx\n", stuck.bcd[0],
                   stuck.bcd[1], stuck.bcd[2], stuck.bcd[3], stuck.bcd[4]);
        else
            printf("HV STUCK: bugcheck n/a (candidate=0x%lx x19=0x%lx)\n", stuck.bcd_va,
                   stuck.regs[19]);
    }
    else if (n <= 4) {
        int g = (n - 2) * 8;    // n = 2,3,4 -> x0-x7, x8-x15, x16-x23
        printf("HV STUCK: x%d-x%d = %016lx %016lx %016lx %016lx\n", g, g + 3,
               stuck.regs[g], stuck.regs[g + 1], stuck.regs[g + 2], stuck.regs[g + 3]);
    } else if (n < 5 + STUCK_CODE) {
        int i = n - 5;
        printf("HV STUCK:   %c0x%lx: %08x\n", i == 4 ? '*' : ' ', stuck.pc + (i - 4) * 4,
               stuck.code[i]);
    } else if (n < 5 + STUCK_CODE + STUCK_FRAMES) {
        int i = n - 5 - STUCK_CODE;
        if (i < stuck.frames)
            printf("HV STUCK: frame %d: lr=0x%lx setup=%08x\n", i, stuck.frame_lr[i],
                   stuck.frame_code[i]);
    } else if (n < 5 + STUCK_CODE + STUCK_FRAMES + 1) {
        if (stuck.octx_ok)
            printf("HV STUCK: orig fp=0x%lx lr=0x%lx sp=0x%lx pc=0x%lx\n", stuck.octx[0],
                   stuck.octx[1], stuck.octx[2], stuck.octx[3]);
    } else {
        //
        // The faulting-instruction block. Wait here until the KTRAP_FRAME walk and the
        // code reads have finished (they run one step a tick). Rewind printed so this
        // same line index is retried next tick.
        //
        if (stuck.fpc_state != 9) {
            stuck.printed--;
            return;
        }
        int i = n - (5 + STUCK_CODE + STUCK_FRAMES + 1);
        if (i == 0)
            printf("HV STUCK: trap=0x%lx fault_pc=0x%lx tf_esr=0x%lx far=0x%lx\n",
                   stuck.trap_ptr, stuck.fault_pc, stuck.tf_esr, stuck.far);
        else {
            int j = i - 1;
            if (stuck.fpc_pa && j < stuck.fpc_read)
                printf("HV STUCK:   %c0x%lx: %08x\n", j == 4 ? '*' : ' ',
                       stuck.fault_pc + (j - 4) * 4, stuck.fpc_code[j]);
        }
    }
}

static void hv_sample_pc(struct exc_info *ctx)
{
    static u64 counter = 0;
    static u64 last_pc = 0;
    static u32 repeats = 0;

    if (!hv_pc_sampling)
        return;

    if (++counter % (HV_TICK_RATE / 2))
        return;

    //
    // Once a report is latched, keep draining it out on every tick regardless of where
    // the guest's PC wanders. A guest that is *looping* (a busy-wait) rather than spinning
    // on one instruction otherwise re-triggered collection endlessly and never finished
    // printing the frames - which is exactly where the bugcheck code lives.
    //
    if (stuck.valid) {
        hv_read_stack_slice();
        hv_print_stuck_line();
        //
        // Once the whole report is drained, release the latch and resume normal
        // sampling. A genuinely wedged guest re-collects the same report next tick
        // (confirming the hang); a guest that only spun briefly with interrupts masked
        // (a spinlock) is seen to move on. Without this, the latch blinds us to a guest
        // that is actually still making progress.
        //
        if (stuck.printed >= STUCK_LINES) {
            stuck.valid = false;
            last_pc = 0;
            repeats = 0;
        }
        return;
    }

    if (ctx->elr == last_pc) {
        //
        // The vUART link dies seconds after the Windows kernel stops, so for kernel
        // addresses the report cannot afford the usual four-second confirmation: two
        // repeats (one second) and collect. Firmware-time waits keep the longer fuse -
        // they resolve on their own and would otherwise spam false reports.
        //
        //
        // Only a PC that repeats *with interrupts masked* (SPSR.I = bit 7) is a real
        // stop - a bugcheck/halt spin runs with DAIF set. A microsecond delay/poll loop
        // (KeStallExecutionProcessor, UEFI waits) keeps interrupts enabled and must not
        // be mistaken for a hang; that false positive spammed the log before.
        //
        u32 need = ((ctx->elr >> 40) == 0xfffff8) ? 2 : 8;
        if (!repeats && (ctx->elr >> 40) == 0xfffff8) {
            u64 held_pa = hv_translate(ctx->elr & ~3UL, false, false, NULL);
            printf("HV SAMPLE: held pc=0x%lx insn=%08x\n", ctx->elr,
                   held_pa ? read32(held_pa) : 0);
        }
        if (++repeats >= need && (ctx->spsr & BIT(7))) {
            //
            // Skip the CPU idle loop. A repeating PC with interrupts masked, sitting one
            // instruction past a WFI, is HalProcessorIdle (dsb; isb; wfi; ...), not a
            // hang - the core is asleep with nothing to run and wakes each timer tick.
            //
            u64 pa = hv_translate((ctx->elr - 4) & ~3UL, false, false, NULL);
            if (!pa || read32(pa) != 0xd503207f /* wfi */)
                hv_collect_stuck(ctx);
        }
    } else {
        if (repeats)
            printf("HV SAMPLE: pc=0x%lx held for %u samples\n", last_pc, repeats + 1);
        printf("HV SAMPLE: pc=0x%lx spsr=0x%lx sp=0x%lx\n", ctx->elr, ctx->spsr, ctx->sp[1]);
        last_pc = ctx->elr;
        repeats = 0;
    }
}

void hv_tick(struct exc_info *ctx)
{
    hv_wdt_pet();
    hv_sample_pc(ctx);
    iodev_handle_events(uartproxy_iodev);
    if (iodev_can_read(uartproxy_iodev)) {
        printf("HV: User interrupt\n");
        iodev_console_flush();
        if (hv_pinned_cpu == -1 || hv_pinned_cpu == smp_id())
            hv_exc_proxy(ctx, START_HV, HV_USER_INTERRUPT, NULL);
    }
    hv_vuart_poll();
    hv_fb_stream_tick(&hv_framebuffer_stream);
}
