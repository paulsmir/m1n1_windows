/* SPDX-License-Identifier: MIT */
#ifndef HV_AGX_LOCAL_RESERVE_H
#define HV_AGX_LOCAL_RESERVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef AGX_LOCAL_RESERVE_V2
#define HV_AGX_LOCAL_BYTES UINT64_C(0x40000000)
#define HV_AGX_LOCAL_ALIGNMENT UINT64_C(0x10000)
#define HV_AGX_LOCAL_ABI_VERSION UINT32_C(2)
#else
#define HV_AGX_LOCAL_BYTES UINT64_C(0x4000000)
#define HV_AGX_LOCAL_ALIGNMENT HV_AGX_LOCAL_BYTES
#define HV_AGX_LOCAL_ABI_VERSION UINT32_C(1)
#endif
#define HV_AGX_LOCAL_LEAF_BYTES UINT64_C(0x4000)
#define HV_AGX_LOCAL_PHYSICAL_LIMIT (UINT64_C(1) << 40)
#define HV_AGX_LOCAL_ABI_MAGIC UINT32_C(0x4c584741)
#define HV_AGX_LOCAL_MMIO_OFFSET UINT64_C(0xd00)
#define HV_AGX_LOCAL_MMIO_BYTES UINT64_C(0x28)
#define HV_AGX_LOCAL_REG_MAGIC UINT64_C(0xd00)
#define HV_AGX_LOCAL_REG_VERSION UINT64_C(0xd04)
#define HV_AGX_LOCAL_REG_VALID UINT64_C(0xd08)
#define HV_AGX_LOCAL_REG_GUEST_IPA UINT64_C(0xd10)
#define HV_AGX_LOCAL_REG_HOST_PA UINT64_C(0xd18)
#define HV_AGX_LOCAL_REG_BYTES UINT64_C(0xd20)

struct hv_agx_local_range {
    uint64_t base;
    uint64_t bytes;
};

struct hv_agx_local_receipt {
    uint64_t guest_ipa;
    uint64_t host_pa;
    uint64_t bytes;
};

bool hv_agx_local_public_span(uint64_t *base, uint64_t *bytes);

enum hv_agx_local_reason {
    HV_AGX_LOCAL_OK,
    HV_AGX_LOCAL_ARGUMENT,
    HV_AGX_LOCAL_OVERLAP,
    HV_AGX_LOCAL_UNMAPPED,
    HV_AGX_LOCAL_PA_ALIGNMENT,
    HV_AGX_LOCAL_PA_LIMIT,
    HV_AGX_LOCAL_NONCONTIGUOUS,
};

struct hv_agx_local_failure {
    enum hv_agx_local_reason reason;
    uint64_t ipa;
    uint64_t pa;
};

typedef bool (*hv_agx_local_translate_fn)(void *context, uint64_t ipa, uint64_t *pa);

/* Only the ADT normal-RAM owner may contain the reserve. Other (including
 * unknown) carveout owners must be disjoint. No address is an allowlist. */
bool hv_agx_local_scanout_allows(const struct hv_agx_local_receipt *receipt,
                                  uint64_t pa, uint64_t bytes);

bool hv_agx_local_carveout_allows(uint64_t candidate, uint64_t base,
                                  uint64_t bytes, bool normal_ram);

bool hv_agx_local_validate(uint64_t guest_ipa,
                           const struct hv_agx_local_range *excluded, size_t excluded_count,
                           hv_agx_local_translate_fn translate, void *context,
                           struct hv_agx_local_receipt *result);

bool hv_agx_local_select(uint64_t ram_base, uint64_t ram_bytes,
                         const struct hv_agx_local_range *excluded, size_t excluded_count,
                         hv_agx_local_translate_fn translate, void *context,
                         struct hv_agx_local_receipt *result);

bool hv_agx_local_select_detailed(uint64_t ram_base, uint64_t ram_bytes,
                                  const struct hv_agx_local_range *excluded,
                                  size_t excluded_count,
                                  hv_agx_local_translate_fn translate, void *context,
                                  struct hv_agx_local_receipt *result,
                                  struct hv_agx_local_failure *failure);

bool hv_agx_local_receipt_mmio(const struct hv_agx_local_receipt *receipt,
                                uint64_t offset, bool write, unsigned width,
                                uint64_t *value);

#endif
