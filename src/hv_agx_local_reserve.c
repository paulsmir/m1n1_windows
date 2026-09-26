/* SPDX-License-Identifier: MIT */
#include "hv_agx_local_reserve.h"

static bool overlaps(uint64_t base, uint64_t bytes,
                     const struct hv_agx_local_range *range)
{
    if (!range->bytes || range->base > UINT64_MAX - range->bytes)
        return true;
    return base < range->base + range->bytes && range->base < base + bytes;
}

bool hv_agx_local_validate(uint64_t guest_ipa,
                           const struct hv_agx_local_range *excluded, size_t excluded_count,
                           hv_agx_local_translate_fn translate, void *context,
                           struct hv_agx_local_receipt *result)
{
    uint64_t first_pa = 0;

    if (!result || !translate || (excluded_count && !excluded) ||
        (guest_ipa & (HV_AGX_LOCAL_BYTES - 1)) ||
        guest_ipa > UINT64_MAX - HV_AGX_LOCAL_BYTES)
        return false;
    *result = (struct hv_agx_local_receipt){0};
    for (size_t i = 0; i < excluded_count; i++) {
        if (overlaps(guest_ipa, HV_AGX_LOCAL_BYTES, &excluded[i]))
            return false;
    }
    for (uint64_t offset = 0; offset < HV_AGX_LOCAL_BYTES;
         offset += HV_AGX_LOCAL_LEAF_BYTES) {
        uint64_t pa = 0;
        if (!translate(context, guest_ipa + offset, &pa) || !pa ||
            (pa & (HV_AGX_LOCAL_LEAF_BYTES - 1)))
            return false;
        if (!offset) {
            first_pa = pa;
            if ((first_pa & (HV_AGX_LOCAL_BYTES - 1)) ||
                first_pa >= HV_AGX_LOCAL_PHYSICAL_LIMIT ||
                HV_AGX_LOCAL_BYTES > HV_AGX_LOCAL_PHYSICAL_LIMIT - first_pa)
                return false;
        } else if (pa != first_pa + offset) {
            return false;
        }
    }
    *result = (struct hv_agx_local_receipt){guest_ipa, first_pa, HV_AGX_LOCAL_BYTES};
    return true;
}

bool hv_agx_local_select(uint64_t ram_base, uint64_t ram_bytes,
                         const struct hv_agx_local_range *excluded, size_t excluded_count,
                         hv_agx_local_translate_fn translate, void *context,
                         struct hv_agx_local_receipt *result)
{
    uint64_t end, candidate;

    if (!result || !ram_bytes || ram_base > UINT64_MAX - ram_bytes ||
        ram_base > UINT64_MAX - (HV_AGX_LOCAL_BYTES - 1))
        return false;
    *result = (struct hv_agx_local_receipt){0};
    end = ram_base + ram_bytes;
    candidate = (ram_base + HV_AGX_LOCAL_BYTES - 1) & ~(HV_AGX_LOCAL_BYTES - 1);
    while (candidate <= end && HV_AGX_LOCAL_BYTES <= end - candidate) {
        if (hv_agx_local_validate(candidate, excluded, excluded_count,
                                  translate, context, result))
            return true;
        if (candidate > UINT64_MAX - HV_AGX_LOCAL_BYTES)
            break;
        candidate += HV_AGX_LOCAL_BYTES;
    }
    return false;
}

bool hv_agx_local_receipt_mmio(const struct hv_agx_local_receipt *receipt,
                                uint64_t offset, bool write, unsigned width,
                                uint64_t *value)
{
    if (!receipt || !value || write)
        return false;
    if (width == 2) {
        switch (offset) {
            case HV_AGX_LOCAL_REG_MAGIC:
                *value = HV_AGX_LOCAL_ABI_MAGIC;
                return true;
            case HV_AGX_LOCAL_REG_VERSION:
                *value = HV_AGX_LOCAL_ABI_VERSION;
                return true;
            case HV_AGX_LOCAL_REG_VALID:
                *value = receipt->bytes == HV_AGX_LOCAL_BYTES && receipt->guest_ipa &&
                         receipt->host_pa;
                return true;
            default:
                return false;
        }
    }
    if (width == 3) {
        switch (offset) {
            case HV_AGX_LOCAL_REG_GUEST_IPA:
                *value = receipt->guest_ipa;
                return true;
            case HV_AGX_LOCAL_REG_HOST_PA:
                *value = receipt->host_pa;
                return true;
            case HV_AGX_LOCAL_REG_BYTES:
                *value = receipt->bytes;
                return true;
            default:
                return false;
        }
    }
    return false;
}
