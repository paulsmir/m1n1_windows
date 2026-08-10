/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

bool hv_nvme_bar_contains(uint64_t base, uint64_t size, uint64_t ipa);

int main(void)
{
    const uint64_t base = 0x400000000ULL;
    const uint64_t size = 0x1000000ULL;

    assert(!hv_nvme_bar_contains(0, size, base));
    assert(!hv_nvme_bar_contains(base, 0, base));
    assert(!hv_nvme_bar_contains(base, size, base - 1));
    assert(hv_nvme_bar_contains(base, size, base));
    assert(hv_nvme_bar_contains(base, size, base + size - 1));
    assert(!hv_nvme_bar_contains(base, size, base + size));

    /* The subtraction form must not wrap at the end of the address space. */
    assert(hv_nvme_bar_contains(UINT64_MAX - 0xff, 0x100, UINT64_MAX));
    assert(!hv_nvme_bar_contains(UINT64_MAX - 0xff, 0x100, 0));

    return 0;
}
