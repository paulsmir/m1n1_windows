#include <assert.h>
#include <stdio.h>

#include "../src/hv_xhci_handoff.h"

struct fake_stage2 {
    uint64_t ipa_base;
    uint64_t pages[6];
};

static uint64_t fake_translate(uint64_t ipa, void *opaque)
{
    struct fake_stage2 *stage2 = opaque;

    if (ipa < stage2->ipa_base || ipa >= stage2->ipa_base + sizeof(stage2->pages) * 0x4000)
        return 0;
    return stage2->pages[(ipa - stage2->ipa_base) / 0x4000];
}

int main(void)
{
    struct hv_xhci_handoff_clear clear = {0};

    /* Measured J313 handoff: halted controller, HSE|PCD stale, interrupter disabled. */
    assert(hv_xhci_handoff_clear_plan(0x0, 0x15, 0x0, false, &clear));
    assert(clear.reset);
    assert(clear.usbsts_w1c == 0x14);
    assert(clear.iman_w1c == 0x0);

    assert(hv_xhci_handoff_clear_plan(0x0, 0x11, 0x0, false, &clear));
    assert(!clear.reset);

    /* Never clear status once either controller-level interrupt path is armed. */
    assert(!hv_xhci_handoff_clear_plan(0x5, 0x1c, 0x0, false, &clear));
    assert(!hv_xhci_handoff_clear_plan(0x0, 0x1c, 0x2, false, &clear));

    /* Never reset a controller after a new owner has programmed its DMA rings. */
    assert(!hv_xhci_handoff_clear_plan(0x0, 0x15, 0x0, true, &clear));

    /* Coalesce only genuinely contiguous IPA -> PA mappings and skip stage-2 holes. */
    struct fake_stage2 stage2 = {
        .ipa_base = 0x100000,
        .pages = {0x8a0100000, 0x8a0104000, 0, 0x8a0200000, 0x8a0204000, 0x8a0300000},
    };
    struct hv_xhci_dma_run run;

    assert(hv_xhci_dma_next_run(0x100000, 0x118000, fake_translate, &stage2, &run));
    assert(run.iova == 0x100000);
    assert(run.paddr == 0x8a0100000);
    assert(run.size == 0x8000);
    assert(run.next_iova == 0x108000);

    assert(hv_xhci_dma_next_run(run.next_iova, 0x118000, fake_translate, &stage2, &run));
    assert(run.iova == 0x10c000);
    assert(run.paddr == 0x8a0200000);
    assert(run.size == 0x8000);
    assert(run.next_iova == 0x114000);

    assert(hv_xhci_dma_next_run(run.next_iova, 0x118000, fake_translate, &stage2, &run));
    assert(run.iova == 0x114000);
    assert(run.paddr == 0x8a0300000);
    assert(run.size == 0x4000);
    assert(run.next_iova == 0x118000);
    assert(!hv_xhci_dma_next_run(run.next_iova, 0x118000, fake_translate, &stage2, &run));

    assert(!hv_xhci_dma_next_run(0x100001, 0x118000, fake_translate, &stage2, &run));

    /*
     * T8103 DART has a 32-bit input address space.  LOW_MEM DMA stays direct,
     * while a high UEFI address such as 0x8f392f000 reaches DART as 0xf392f000.
     */
    assert(hv_xhci_dma_guest_ipa(0x01000000, 0x40000000, 0x800000000) == 0x01000000);
    assert(hv_xhci_dma_guest_ipa(0x3fffffff, 0x40000000, 0x800000000) == 0x3fffffff);
    assert(hv_xhci_dma_guest_ipa(0x40000000, 0x40000000, 0x800000000) == 0x840000000);
    assert(hv_xhci_dma_guest_ipa(0xf392f000, 0x40000000, 0x800000000) == 0x8f392f000);

    /*
     * USBXHCI may program a 64-bit DMA register as two 32-bit MMIO writes.
     * Preserve both halves so the diagnostic log reports the guest address,
     * even when the physical T8103 controller later discards the high half.
     */
    struct hv_xhci_dma_trace trace = {0};
    assert(hv_xhci_dma_trace_write(&trace, 0x20, 0x1000, 0x38, 0x00123001, 2) ==
           HV_XHCI_DMA_REG_CRCR);
    assert(trace.crcr == 0x00123001);
    assert(hv_xhci_dma_trace_write(&trace, 0x20, 0x1000, 0x3c, 0x00000009, 2) ==
           HV_XHCI_DMA_REG_CRCR);
    assert(trace.crcr == 0x0000000900123001ULL);

    assert(hv_xhci_dma_trace_write(&trace, 0x20, 0x1000, 0x50, 0x00000008f3733000ULL,
                                   3) == HV_XHCI_DMA_REG_DCBAAP);
    assert(trace.dcbaap == 0x00000008f3733000ULL);
    assert(hv_xhci_dma_trace_write(&trace, 0x20, 0x1000, 0x1030,
                                   0x00000008f372d000ULL, 3) ==
           HV_XHCI_DMA_REG_ERSTBA);
    assert(trace.erstba == 0x00000008f372d000ULL);
    assert(hv_xhci_dma_trace_write(&trace, 0x20, 0x1000, 0x1038, 0xf372e000, 2) ==
           HV_XHCI_DMA_REG_ERDP);
    assert(hv_xhci_dma_trace_write(&trace, 0x20, 0x1000, 0x103c, 0x00000008, 2) ==
           HV_XHCI_DMA_REG_ERDP);
    assert(trace.erdp == 0x00000008f372e000ULL);

    assert(hv_xhci_dma_trace_write(&trace, 0x20, 0x1000, 0x420, 0x000202a0, 2) ==
           HV_XHCI_DMA_REG_NONE);

    /* The T8103 DART cannot honor the 64-bit DMA addresses advertised by xHCI. */
    assert(hv_xhci_cap_read_for_guest(0x10, 0x0238ffcd, 2) == 0x0238ffcc);
    assert(hv_xhci_cap_read_for_guest(0x0c, 0x0238ffcd00000000ULL, 3) ==
           0x0238ffcc00000000ULL);
    assert(hv_xhci_cap_read_for_guest(0x14, 0x000004e0, 2) == 0x000004e0);

    puts("hv_xhci_handoff_test: ok");
    return 0;
}
