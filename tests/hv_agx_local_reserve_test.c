#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "../src/hv_agx_local_reserve.h"

struct mapping {
    uint64_t break_ipa;
    uint64_t unmapped_ipa;
};

static bool translate(void *opaque, uint64_t ipa, uint64_t *pa)
{
    struct mapping *map = opaque;
    if (ipa == map->unmapped_ipa)
        return false;
    *pa = ipa + (ipa == map->break_ipa ? HV_AGX_LOCAL_LEAF_BYTES : 0);
    return true;
}

int main(void)
{
    const uint64_t ram = UINT64_C(0x850000000);
    const struct hv_agx_local_range excluded[] = {
        {ram, UINT64_C(0x90000000)},
        {ram + UINT64_C(0x90004000), UINT64_C(0x4000)},
    };
    struct mapping map = {0};
    struct hv_agx_local_receipt selected = {0};
    struct hv_agx_local_receipt valid;
    const uint64_t expected = ram + UINT64_C(0x94000000);

    assert(hv_agx_local_select(ram, UINT64_C(0x100000000), excluded,
                               sizeof(excluded) / sizeof(excluded[0]),
                               translate, &map, &selected));
    assert(selected.guest_ipa == expected);
    assert(selected.host_pa == expected);
    assert(selected.bytes == HV_AGX_LOCAL_BYTES);
    valid = selected;
    assert(!hv_agx_local_validate(ram + UINT64_C(0x90000000),
                                   excluded, sizeof(excluded) / sizeof(excluded[0]),
                                   translate, &map, &selected));

    map.break_ipa = expected + HV_AGX_LOCAL_LEAF_BYTES;
    assert(!hv_agx_local_validate(expected, excluded,
                                   sizeof(excluded) / sizeof(excluded[0]),
                                   translate, &map, &selected));
    assert(!hv_agx_local_select(UINT64_MAX - HV_AGX_LOCAL_BYTES + 1,
                                HV_AGX_LOCAL_BYTES, excluded, 0,
                                translate, &map, &selected));

    /* EXP827 live J313 inputs: low backing ends at the first eligible slab. */
    {
        const uint64_t candidate = UINT64_C(0x8e0000000);
        const struct hv_agx_local_range live_excluded[] = {
            {UINT64_C(0x850000000), UINT64_C(0x90000000)},
            {UINT64_C(0x9fff78000), UINT64_C(0x40000)},
        };
        struct hv_agx_local_failure failure = {0};
        map = (struct mapping){.unmapped_ipa = candidate};
        assert(!hv_agx_local_select_detailed(candidate, HV_AGX_LOCAL_BYTES,
                                             live_excluded, 2, translate, &map,
                                             &selected, &failure));
        assert(failure.reason == HV_AGX_LOCAL_UNMAPPED);
        assert(failure.ipa == candidate);
        map = (struct mapping){0};
        assert(hv_agx_local_select_detailed(candidate, HV_AGX_LOCAL_BYTES,
                                            live_excluded, 2, translate, &map,
                                            &selected, &failure));
        assert(selected.guest_ipa == candidate);
        assert(selected.host_pa == candidate);
        assert(failure.reason == HV_AGX_LOCAL_OK);
        map.unmapped_ipa = candidate + HV_AGX_LOCAL_LEAF_BYTES;
        assert(!hv_agx_local_select_detailed(candidate, HV_AGX_LOCAL_BYTES,
                                             live_excluded, 2, translate, &map,
                                             &selected, &failure));
        assert(failure.reason == HV_AGX_LOCAL_UNMAPPED);
        assert(failure.ipa == map.unmapped_ipa);
    }

    {
        uint64_t value = 0;
        assert(hv_agx_local_receipt_mmio(&valid, HV_AGX_LOCAL_REG_VERSION,
                                          false, 2, &value));
        assert(value == HV_AGX_LOCAL_ABI_VERSION);
        assert(!hv_agx_local_receipt_mmio(&valid, HV_AGX_LOCAL_REG_VERSION,
                                           true, 2, &value));
        assert(hv_agx_local_receipt_mmio(&valid, HV_AGX_LOCAL_REG_GUEST_IPA,
                                          false, 3, &value));
        assert(value == valid.guest_ipa);
        assert(!hv_agx_local_receipt_mmio(&valid, HV_AGX_LOCAL_REG_GUEST_IPA,
                                           false, 2, &value));
    }
    return 0;
}
