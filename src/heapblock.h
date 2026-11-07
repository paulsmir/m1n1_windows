/* SPDX-License-Identifier: MIT */

#ifndef HEAPBLOCK_H
#define HEAPBLOCK_H

#include "types.h"
#include "heapblock_bound.h"

void heapblock_init(void);

void *heapblock_alloc(size_t size);
void *heapblock_alloc_aligned(size_t size, size_t align);

/* Optional upper bound for every later heapblock (and thus dlmalloc) block.
 * Set once before guest entry so all m1n1-owned memory, including DART page
 * tables allocated after launch, provably stays below the guest window.
 * Returns false if already set, not above the current base, or 0. */
bool heapblock_set_ceiling(void *ceiling);
void *heapblock_get_ceiling(void);
void *heapblock_get_init_base(void);


#endif
