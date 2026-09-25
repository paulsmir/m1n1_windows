/* SPDX-License-Identifier: MIT */

#ifndef HV_GUEST_IPA_PA_H
#define HV_GUEST_IPA_PA_H

#if defined(HV_GUEST_IPA_PA_HOST_TEST)
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t hv_guest_ipa_pa_u32;
typedef uint64_t hv_guest_ipa_pa_u64;
#elif defined(_MSC_VER)
typedef unsigned int hv_guest_ipa_pa_u32;
typedef unsigned __int64 hv_guest_ipa_pa_u64;
#else
#include "types.h"
typedef u32 hv_guest_ipa_pa_u32;
typedef u64 hv_guest_ipa_pa_u64;
#endif

/* Dedicated guest ABI.  Other HVC immediates retain their existing meaning. */
#define HV_GUEST_IPA_PA_HVC_IMMEDIATE 0x4d31u
#define HV_GPUVA_ARM_CONSUMED_HVC_IMMEDIATE 0x4d32u
#define HV_GPUVA_ARM_CONSUMED_VERSION 1u
#define HV_GUEST_IPA_PA_VERSION 1u
#define HV_GUEST_IPA_PA_TRANSLATE 1u
#define HV_GUEST_IPA_PA_MAX_PAGES 64u
#define HV_GUEST_IPA_PA_PAGE_SIZE 0x1000ull
#define HV_GUEST_IPA_PA_STAGE2_LEAF_SIZE 0x4000ull

enum hv_guest_ipa_pa_status {
    HV_GUEST_IPA_PA_STATUS_EMPTY = 0,
    HV_GUEST_IPA_PA_STATUS_SUCCESS = 1,
    HV_GUEST_IPA_PA_STATUS_INVALID_REQUEST = 2,
    HV_GUEST_IPA_PA_STATUS_UNMAPPED = 3,
    HV_GUEST_IPA_PA_STATUS_NOT_RAM = 4,
};

struct hv_guest_ipa_pa_request {
    hv_guest_ipa_pa_u32 version;
    hv_guest_ipa_pa_u32 operation;
    hv_guest_ipa_pa_u32 count;
    hv_guest_ipa_pa_u32 status;
    hv_guest_ipa_pa_u64 ipa[HV_GUEST_IPA_PA_MAX_PAGES];
    hv_guest_ipa_pa_u64 pa[HV_GUEST_IPA_PA_MAX_PAGES];
};

#if !defined(_MSC_VER)
bool hv_guest_arm_consumed_handle(hv_guest_ipa_pa_u32 immediate,
                                  hv_guest_ipa_pa_u64 payload,
                                  hv_guest_ipa_pa_u32 *sequence);
typedef hv_guest_ipa_pa_u64 (*hv_guest_ipa_pa_translate_fn)(hv_guest_ipa_pa_u64 ipa,
                                                            void *opaque);
typedef bool (*hv_guest_ipa_pa_is_ram_fn)(hv_guest_ipa_pa_u64 pa,
                                          hv_guest_ipa_pa_u64 size, void *opaque);

bool hv_guest_ipa_pa_handle(hv_guest_ipa_pa_u32 immediate,
                            hv_guest_ipa_pa_u64 request_ipa,
                            hv_guest_ipa_pa_translate_fn translate,
                            hv_guest_ipa_pa_is_ram_fn is_ram, void *opaque,
                            hv_guest_ipa_pa_u32 *return_status);
#endif

#endif
