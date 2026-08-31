/* SPDX-License-Identifier: MIT */

#include "hv_guest_ipa_pa.h"

static void clear_results(struct hv_guest_ipa_pa_request *request,
                          hv_guest_ipa_pa_u32 count,
                          enum hv_guest_ipa_pa_status status)
{
    if (count > HV_GUEST_IPA_PA_MAX_PAGES)
        count = HV_GUEST_IPA_PA_MAX_PAGES;
    for (hv_guest_ipa_pa_u32 index = 0; index < count; index++)
        request->pa[index] = 0;
    request->status = (hv_guest_ipa_pa_u32)status;
}

bool hv_guest_ipa_pa_handle(hv_guest_ipa_pa_u32 immediate,
                            hv_guest_ipa_pa_u64 request_ipa,
                            hv_guest_ipa_pa_translate_fn translate,
                            hv_guest_ipa_pa_is_ram_fn is_ram, void *opaque,
                            hv_guest_ipa_pa_u32 *return_status)
{
    struct hv_guest_ipa_pa_request *request;
    hv_guest_ipa_pa_u64 request_pa;
    hv_guest_ipa_pa_u64 leaf_offset;
    hv_guest_ipa_pa_u32 count;

    if (immediate != HV_GUEST_IPA_PA_HVC_IMMEDIATE)
        return false;
    if (return_status == 0)
        return true;
    *return_status = HV_GUEST_IPA_PA_STATUS_INVALID_REQUEST;

    leaf_offset = request_ipa & (HV_GUEST_IPA_PA_STAGE2_LEAF_SIZE - 1ull);
    if (translate == 0 || is_ram == 0 ||
        (request_ipa & (sizeof(hv_guest_ipa_pa_u64) - 1ull)) != 0 ||
        leaf_offset > HV_GUEST_IPA_PA_STAGE2_LEAF_SIZE - sizeof(*request))
        return true;

    request_pa = translate(request_ipa, opaque);
    if (request_pa == 0 || !is_ram(request_pa, sizeof(*request), opaque)) {
        *return_status = HV_GUEST_IPA_PA_STATUS_NOT_RAM;
        return true;
    }
    request = (struct hv_guest_ipa_pa_request *)(uintptr_t)request_pa;
    count = request->count;
    if (request->version != HV_GUEST_IPA_PA_VERSION ||
        request->operation != HV_GUEST_IPA_PA_TRANSLATE || count == 0 ||
        count > HV_GUEST_IPA_PA_MAX_PAGES) {
        clear_results(request, count, HV_GUEST_IPA_PA_STATUS_INVALID_REQUEST);
        *return_status = HV_GUEST_IPA_PA_STATUS_INVALID_REQUEST;
        return true;
    }

    clear_results(request, count, HV_GUEST_IPA_PA_STATUS_EMPTY);
    for (hv_guest_ipa_pa_u32 index = 0; index < count; index++) {
        hv_guest_ipa_pa_u64 ipa = request->ipa[index];
        hv_guest_ipa_pa_u64 pa;

        if ((ipa & (HV_GUEST_IPA_PA_PAGE_SIZE - 1ull)) != 0) {
            clear_results(request, count, HV_GUEST_IPA_PA_STATUS_INVALID_REQUEST);
            *return_status = HV_GUEST_IPA_PA_STATUS_INVALID_REQUEST;
            return true;
        }
        pa = translate(ipa, opaque);
        if (pa == 0) {
            clear_results(request, count, HV_GUEST_IPA_PA_STATUS_UNMAPPED);
            *return_status = HV_GUEST_IPA_PA_STATUS_UNMAPPED;
            return true;
        }
        if ((pa & (HV_GUEST_IPA_PA_PAGE_SIZE - 1ull)) != 0 ||
            !is_ram(pa, HV_GUEST_IPA_PA_PAGE_SIZE, opaque)) {
            clear_results(request, count, HV_GUEST_IPA_PA_STATUS_NOT_RAM);
            *return_status = HV_GUEST_IPA_PA_STATUS_NOT_RAM;
            return true;
        }
        request->pa[index] = pa;
    }

    request->status = HV_GUEST_IPA_PA_STATUS_SUCCESS;
    *return_status = HV_GUEST_IPA_PA_STATUS_SUCCESS;
    return true;
}
