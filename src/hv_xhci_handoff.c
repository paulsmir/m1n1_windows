/* SPDX-License-Identifier: MIT */

#include "hv_xhci_handoff.h"

#define XHCI_USBCMD_RUN  (1U << 0)
#define XHCI_USBCMD_INTE (1U << 2)
#define XHCI_USBSTS_HCH  (1U << 0)
#define XHCI_USBSTS_HSE  (1U << 2)
#define XHCI_USBSTS_W1C  ((1U << 2) | (1U << 3) | (1U << 4))
#define XHCI_IMAN_IP     (1U << 0)
#define XHCI_IMAN_IE     (1U << 1)

bool hv_xhci_handoff_clear_plan(u32 usbcmd, u32 usbsts, u32 iman, bool dma_programmed,
                                struct hv_xhci_handoff_clear *clear)
{
    if (!clear || dma_programmed || (usbcmd & (XHCI_USBCMD_RUN | XHCI_USBCMD_INTE)) ||
        (iman & XHCI_IMAN_IE))
        return false;

    clear->reset = (usbsts & (XHCI_USBSTS_HCH | XHCI_USBSTS_HSE)) ==
                   (XHCI_USBSTS_HCH | XHCI_USBSTS_HSE);
    clear->usbsts_w1c = usbsts & XHCI_USBSTS_W1C;
    clear->iman_w1c = iman & XHCI_IMAN_IP;
    return clear->usbsts_w1c || clear->iman_w1c;
}

u64 hv_xhci_dma_guest_ipa(u64 iova, u64 direct_end, u64 high_window_base)
{
    if (iova < direct_end)
        return iova;
    return high_window_base + iova;
}

bool hv_xhci_dma_next_run(u64 start, u64 end, hv_xhci_dma_translate_fn translate, void *opaque,
                          struct hv_xhci_dma_run *run)
{
    const u64 page_mask = HV_XHCI_DMA_PAGE_SIZE - 1;

    if (!translate || !run || start >= end || (start & page_mask) || (end & page_mask))
        return false;

    u64 iova = start;
    u64 paddr = 0;
    while (iova < end) {
        paddr = translate(iova, opaque);
        if (paddr && !(paddr & page_mask))
            break;
        iova += HV_XHCI_DMA_PAGE_SIZE;
    }
    if (iova == end)
        return false;

    run->iova = iova;
    run->paddr = paddr;
    run->size = HV_XHCI_DMA_PAGE_SIZE;
    iova += HV_XHCI_DMA_PAGE_SIZE;

    while (iova < end) {
        paddr = translate(iova, opaque);
        if (paddr != run->paddr + run->size)
            break;
        run->size += HV_XHCI_DMA_PAGE_SIZE;
        iova += HV_XHCI_DMA_PAGE_SIZE;
    }

    run->next_iova = iova;
    return true;
}

enum hv_xhci_dma_reg hv_xhci_dma_trace_write(struct hv_xhci_dma_trace *trace, u32 caplen,
                                             u32 rtsoff, u64 offset, u64 value, int width)
{
    if (!trace || width < 0 || width > 3)
        return HV_XHCI_DMA_REG_NONE;

    struct {
        enum hv_xhci_dma_reg id;
        u64 offset;
        u64 *value;
    } regs[] = {
        {HV_XHCI_DMA_REG_CRCR, caplen + 0x18, &trace->crcr},
        {HV_XHCI_DMA_REG_DCBAAP, caplen + 0x30, &trace->dcbaap},
        {HV_XHCI_DMA_REG_ERSTBA, rtsoff + 0x30, &trace->erstba},
        {HV_XHCI_DMA_REG_ERDP, rtsoff + 0x38, &trace->erdp},
    };

    u64 bytes = 1ULL << width;
    for (u32 i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        if (offset < regs[i].offset || offset + bytes > regs[i].offset + sizeof(u64))
            continue;

        u32 shift = (offset - regs[i].offset) * 8;
        u64 mask = bytes == sizeof(u64) ? ~0ULL : ((1ULL << (bytes * 8)) - 1) << shift;
        *regs[i].value = (*regs[i].value & ~mask) | ((value << shift) & mask);
        return regs[i].id;
    }

    return HV_XHCI_DMA_REG_NONE;
}

const char *hv_xhci_dma_reg_name(enum hv_xhci_dma_reg reg)
{
    switch (reg) {
        case HV_XHCI_DMA_REG_CRCR:
            return "CRCR";
        case HV_XHCI_DMA_REG_DCBAAP:
            return "DCBAAP";
        case HV_XHCI_DMA_REG_ERSTBA:
            return "ERSTBA";
        case HV_XHCI_DMA_REG_ERDP:
            return "ERDP";
        default:
            return "unknown";
    }
}

u64 hv_xhci_cap_read_for_guest(u64 offset, u64 value, int width)
{
    const u64 hccparams1_offset = 0x10;

    if (width < 0 || width > 3)
        return value;

    u64 bytes = 1ULL << width;
    if (offset > hccparams1_offset || offset + bytes <= hccparams1_offset)
        return value;

    u32 bit = (hccparams1_offset - offset) * 8;
    return value & ~(1ULL << bit);
}
