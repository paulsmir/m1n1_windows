/* SPDX-License-Identifier: MIT */
/* Catches an m1n1 allocation that could cross the guest window (DART tables
 * allocated after launch must stay below the published ceiling). */
#include "../src/heapblock_bound.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    const uintptr_t c = 0x818000000u;
    assert(heapblock_block_fits(0x804d90000u, 0x4000, 0));
    assert(heapblock_block_fits(0x804d90000u, 0x4000, c));
    assert(heapblock_block_fits(c - 0x4000, 0x4000, c));      /* ends exactly at ceiling */
    assert(!heapblock_block_fits(c - 0x4000, 0x4001, c));     /* one byte over */
    assert(!heapblock_block_fits(c, 1, c));
    assert(!heapblock_block_fits(c, 0, c));                    /* zero-size probe at ceiling */
    assert(heapblock_block_fits(c - 1, 0, c));
    assert(!heapblock_block_fits(c - 0x10, SIZE_MAX, c));      /* overflow-safe */
    assert(!heapblock_block_fits(UINTPTR_MAX, 2, c));
    puts("heapblock ceiling bound tests PASS");
    return 0;
}
