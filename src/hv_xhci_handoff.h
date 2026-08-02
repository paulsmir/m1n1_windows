/* SPDX-License-Identifier: MIT */

#ifndef HV_XHCI_HANDOFF_H
#define HV_XHCI_HANDOFF_H

#ifdef HV_XHCI_HANDOFF_HOST_TEST
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include "types.h"
#endif

#define HV_XHCI_DMA_PAGE_SIZE 0x4000ULL

struct hv_xhci_handoff_clear {
    bool reset;
    u32 usbsts_w1c;
    u32 iman_w1c;
};

struct hv_xhci_dma_run {
    u64 iova;
    u64 paddr;
    u64 size;
    u64 next_iova;
};

enum hv_xhci_dma_reg {
    HV_XHCI_DMA_REG_NONE,
    HV_XHCI_DMA_REG_CRCR,
    HV_XHCI_DMA_REG_DCBAAP,
    HV_XHCI_DMA_REG_ERSTBA,
    HV_XHCI_DMA_REG_ERDP,
};

struct hv_xhci_dma_trace {
    u64 crcr;
    u64 dcbaap;
    u64 erstba;
    u64 erdp;
};

typedef u64 (*hv_xhci_dma_translate_fn)(u64 ipa, void *opaque);

bool hv_xhci_handoff_clear_plan(u32 usbcmd, u32 usbsts, u32 iman, bool dma_programmed,
                                struct hv_xhci_handoff_clear *clear);
u64 hv_xhci_dma_guest_ipa(u64 iova, u64 direct_end, u64 high_window_base);
bool hv_xhci_dma_next_run(u64 start, u64 end, hv_xhci_dma_translate_fn translate, void *opaque,
                          struct hv_xhci_dma_run *run);
enum hv_xhci_dma_reg hv_xhci_dma_trace_write(struct hv_xhci_dma_trace *trace, u32 caplen,
                                             u32 rtsoff, u64 offset, u64 value, int width);
const char *hv_xhci_dma_reg_name(enum hv_xhci_dma_reg reg);
u64 hv_xhci_cap_read_for_guest(u64 offset, u64 value, int width);

#endif
