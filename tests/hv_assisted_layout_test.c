#include <assert.h>
#include <stdio.h>

#include "../src/hv_assisted_layout.h"

int main(void)
{
    struct hv_assisted_layout layout;

    assert(hv_assisted_layout_compute(0x850000000ULL, 0x5c000, 0x58000,
                                      0x1d88000, 0x56c000, 0x40000,
                                      &layout));
    assert(layout.adt_base == 0x851000000ULL);
    assert(layout.trust_cache_base == 0x85105c000ULL);
    assert(layout.firmware_base == 0x8510b4000ULL);
    assert(layout.sepfw_base == 0x852e3c000ULL);
    assert(layout.preoslog_base == 0x8533a8000ULL);
    assert(layout.boot_args_base == 0x8533e8000ULL);
    assert(layout.top_of_kernel_data == 0x8533ec000ULL);

    assert(!hv_assisted_layout_compute(UINT64_MAX - 0x1000, 0x2000, 0, 0, 0, 0,
                                       &layout));
    assert(!hv_assisted_layout_compute(0, 0, 0, 0, 0, 0, NULL));

    puts("hv_assisted_layout_test: ok");
    return 0;
}
