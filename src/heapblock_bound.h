/* SPDX-License-Identifier: MIT */

#ifndef HEAPBLOCK_BOUND_H
#define HEAPBLOCK_BOUND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Pure bound check: [block, block+size) ends at or below ceiling (0 = none).
 * Overflow-safe; a zero-size "infinite" probe must still start below it. */
static inline bool heapblock_block_fits(uintptr_t block, size_t size, uintptr_t ceiling)
{
    if (!ceiling)
        return true;
    return block <= ceiling && size <= ceiling - block && (size || block < ceiling);
}

#endif
