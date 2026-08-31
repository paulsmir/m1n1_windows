#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/hv_guest_ipa_pa.h"

struct fake_translation {
    uint64_t request_ipa;
    struct hv_guest_ipa_pa_request *request;
    uint64_t ipa[HV_GUEST_IPA_PA_MAX_PAGES];
    uint64_t pa[HV_GUEST_IPA_PA_MAX_PAGES];
    uint32_t count;
    uint64_t ram_base;
    uint64_t ram_size;
};

static uint64_t translate(uint64_t ipa, void *opaque)
{
    struct fake_translation *fake = opaque;

    if (ipa == fake->request_ipa)
        return (uint64_t)(uintptr_t)fake->request;
    for (uint32_t index = 0; index < fake->count; index++) {
        if (ipa == fake->ipa[index])
            return fake->pa[index];
    }
    return 0;
}

static bool is_ram(uint64_t pa, uint64_t size, void *opaque)
{
    struct fake_translation *fake = opaque;

    if (pa == (uint64_t)(uintptr_t)fake->request)
        return size == sizeof(*fake->request);
    return size != 0 && pa >= fake->ram_base && size <= fake->ram_size &&
           pa - fake->ram_base <= fake->ram_size - size;
}

static void prepare(struct fake_translation *fake, struct hv_guest_ipa_pa_request *request)
{
    memset(fake, 0, sizeof(*fake));
    memset(request, 0, sizeof(*request));
    fake->request_ipa = 0x2030000;
    fake->request = request;
    fake->ram_base = 0x800000000ULL;
    fake->ram_size = 0x100000000ULL;
    request->version = HV_GUEST_IPA_PA_VERSION;
    request->operation = HV_GUEST_IPA_PA_TRANSLATE;
}

static void test_translates_complete_batch(void)
{
    struct hv_guest_ipa_pa_request request;
    struct fake_translation fake;
    uint32_t result = 0;

    prepare(&fake, &request);
    request.count = fake.count = 4;
    for (uint32_t index = 0; index < request.count; index++) {
        request.ipa[index] = fake.ipa[index] = 0x2000000ULL + index * 0x1000ULL;
        fake.pa[index] = 0x8a0000000ULL + index * 0x1000ULL;
    }

    assert(hv_guest_ipa_pa_handle(HV_GUEST_IPA_PA_HVC_IMMEDIATE, fake.request_ipa,
                                   translate, is_ram, &fake, &result));
    assert(result == HV_GUEST_IPA_PA_STATUS_SUCCESS);
    assert(request.status == HV_GUEST_IPA_PA_STATUS_SUCCESS);
    for (uint32_t index = 0; index < request.count; index++)
        assert(request.pa[index] == fake.pa[index]);
}

static void test_failure_clears_partial_results(void)
{
    struct hv_guest_ipa_pa_request request;
    struct fake_translation fake;
    uint32_t result = 0;

    prepare(&fake, &request);
    request.count = fake.count = 3;
    for (uint32_t index = 0; index < request.count; index++) {
        request.ipa[index] = fake.ipa[index] = 0x3000000ULL + index * 0x1000ULL;
        fake.pa[index] = 0x8b0000000ULL + index * 0x1000ULL;
        request.pa[index] = UINT64_MAX;
    }
    fake.pa[1] = 0;

    assert(hv_guest_ipa_pa_handle(HV_GUEST_IPA_PA_HVC_IMMEDIATE, fake.request_ipa,
                                   translate, is_ram, &fake, &result));
    assert(result == HV_GUEST_IPA_PA_STATUS_UNMAPPED);
    assert(request.status == HV_GUEST_IPA_PA_STATUS_UNMAPPED);
    for (uint32_t index = 0; index < request.count; index++)
        assert(request.pa[index] == 0);
}

static void test_rejects_non_ram_and_cross_leaf_request(void)
{
    struct hv_guest_ipa_pa_request request;
    struct fake_translation fake;
    uint32_t result = 0;

    prepare(&fake, &request);
    request.count = fake.count = 1;
    request.ipa[0] = fake.ipa[0] = 0x4000000;
    fake.pa[0] = 0x500000000ULL;
    assert(hv_guest_ipa_pa_handle(HV_GUEST_IPA_PA_HVC_IMMEDIATE, fake.request_ipa,
                                   translate, is_ram, &fake, &result));
    assert(result == HV_GUEST_IPA_PA_STATUS_NOT_RAM);
    assert(request.pa[0] == 0);

    fake.request_ipa = 0x2033ff0;
    assert(hv_guest_ipa_pa_handle(HV_GUEST_IPA_PA_HVC_IMMEDIATE, fake.request_ipa,
                                   translate, is_ram, &fake, &result));
    assert(result == HV_GUEST_IPA_PA_STATUS_INVALID_REQUEST);
}

static void test_ignores_unrelated_hvc(void)
{
    struct hv_guest_ipa_pa_request request;
    struct fake_translation fake;
    uint32_t result = UINT32_MAX;

    prepare(&fake, &request);
    assert(!hv_guest_ipa_pa_handle(0, fake.request_ipa, translate, is_ram, &fake, &result));
    assert(result == UINT32_MAX);
}

int main(void)
{
    test_translates_complete_batch();
    test_failure_clears_partial_results();
    test_rejects_non_ram_and_cross_leaf_request();
    test_ignores_unrelated_hvc();
    puts("hv_guest_ipa_pa_test: ok");
    return 0;
}
