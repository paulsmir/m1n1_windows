/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/iova_aligned_fit.h"

int main(void)
{
    uint64_t start, prefix, suffix;

    assert(iova_aligned_fit(0x100000, 0x200000, 0x100000, 0x10000,
                            &start, &prefix, &suffix));
    assert(start == 0x100000 && prefix == 0 && suffix == 0x100000);

    assert(iova_aligned_fit(0x104000, 0x200000, 0x100000, 0x10000,
                            &start, &prefix, &suffix));
    assert(start == 0x110000 && prefix == 0xc000 && suffix == 0xf4000);

    assert(!iova_aligned_fit(0x104000, 0x100000, 0x100000, 0x10000,
                             &start, &prefix, &suffix));
    assert(!iova_aligned_fit(UINT64_MAX - 0x1000, 0x2000, 0x1000, 0x10000,
                             &start, &prefix, &suffix));
    assert(!iova_aligned_fit(0x100000, 0x200000, 0x100000, 0x18000,
                             &start, &prefix, &suffix));

    puts("iova_aligned_fit_test: ok");
    return 0;
}
