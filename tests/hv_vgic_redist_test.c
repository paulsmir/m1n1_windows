#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/hv_vgic_redist.h"

static void test_decodes_every_redistributor_frame(void)
{
    const uint64_t base = 0xf10000000ULL;
    struct hv_vgic_redist_addr decoded = {0};

    assert(hv_vgic_redist_decode(base + 0x10000, base, 8, &decoded));
    assert(decoded.cpu == 0);
    assert(decoded.reg == 0x10000);

    assert(hv_vgic_redist_decode(base + 3 * HV_VGIC_REDIST_STRIDE + 0x10104,
                                 base, 8, &decoded));
    assert(decoded.cpu == 3);
    assert(decoded.reg == 0x10104);

    assert(hv_vgic_redist_decode(base + 7 * HV_VGIC_REDIST_STRIDE + 0xffd0,
                                 base, 8, &decoded));
    assert(decoded.cpu == 7);
    assert(decoded.reg == 0xffd0);
}

static void test_rejects_addresses_outside_mapped_frames(void)
{
    const uint64_t base = 0xf10000000ULL;
    struct hv_vgic_redist_addr decoded = {99, 99};

    assert(!hv_vgic_redist_decode(base - 1, base, 8, &decoded));
    assert(!hv_vgic_redist_decode(base + 8 * HV_VGIC_REDIST_STRIDE,
                                  base, 8, &decoded));
    assert(!hv_vgic_redist_decode(base, base, 0, &decoded));
    assert(!hv_vgic_redist_decode(base, base, 8, NULL));
}

int main(void)
{
    test_decodes_every_redistributor_frame();
    test_rejects_addresses_outside_mapped_frames();
    puts("hv_vgic_redist_test: ok");
    return 0;
}
