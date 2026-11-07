/* SPDX-License-Identifier: MIT */
#ifndef HV_ANS_PRP_H
#define HV_ANS_PRP_H
#include "hv_ans_broker_abi.h"
#include <stdbool.h>
#include <stddef.h>
/* translate must validate FULL range, owner RAM, protected ranges and direction.
 * The caller holds mapping lifetime stable until DMA is stopped and TCB reaped.
 * read_metadata copies descriptor bytes only, never payload, from validated PA. */
struct ans_memory_ops {
    bool (*translate)(void *ctx, uint64_t ipa, size_t size, uint32_t direction, uint64_t *pa);
    bool (*read_metadata)(void *ctx, uint64_t pa, void *dst, size_t size);
};
struct ans_prp_shadow {
    uint64_t host_pages[ANS_MAX_DATA_PAGES];
    uint32_t page_count;
    uint32_t length;
};
int ans_prp_translate(const struct ans_memory_ops *ops, void *ctx, uint64_t prp1,
                      uint64_t prp2, uint32_t length, uint32_t direction,
                      struct ans_prp_shadow *out);
#endif
