/* SPDX-License-Identifier: MIT */
#ifndef HV_ANS_MEMORY_H
#define HV_ANS_MEMORY_H
#include "hv_ans_broker_abi.h"
#include "hv_launch_contract.h"

struct ans_memory_range { uint64_t base, size; };
/* raw_leaf must read authoritative page tables (e.g. hv_stage2_get_leaf), not
 * hv_pt_walk/hv_ipa_to_pa or the diagnostic mapping history. shift is 14 or 25.
 * All fields are trusted EL2 state, never guest-selected. The owner must hold
 * the stage-2 mutation lock for this call, and pin mappings/backing/reservations
 * through DMA stop + TCB invalidation. This helper acquires no lock or pin. */
struct ans_memory_context {
    const struct hv_contract_snapshot *launch;
    const struct ans_memory_range *private_pa;
    size_t private_count;
    struct ans_memory_range ramdisk;
    bool reservations_complete;
    uint8_t ipa_bits;
    void *leaf_context;
    bool (*raw_leaf)(void *ctx, uint64_t ipa, uint64_t *descriptor, uint8_t *shift);
};
#define ANS_MEMORY_MAX_PRIVATE_RANGES 64u
/* Suitable directly for ans_memory_ops.translate. Accepts metadata and payload
 * spans, including spans crossing a 16 KiB stage-2 boundary. No copies/DMA. */
bool ans_memory_context_valid(const struct ans_memory_context *);
bool ans_memory_translate(void *context, uint64_t ipa, size_t bytes,
                          uint32_t direction, uint64_t *pa);
#endif
