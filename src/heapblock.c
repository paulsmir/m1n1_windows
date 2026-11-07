/* SPDX-License-Identifier: MIT */

#include "heapblock.h"
#include "assert.h"
#include "types.h"
#include "utils.h"
#include "xnuboot.h"

/*
 * This is a non-freeing allocator, used as a backend for malloc and for uncompressing data.
 *
 * Allocating 0 bytes is allowed, and guarantees "infinite" (until the end of RAM) space is
 * available at the returned pointer as long as no other malloc/heapblock calls occur, which is
 * useful as a buffer for unknown-length uncompressed data. A subsequent call with a size will then
 * actually reserve the block.
 */

static void *heap_base;
static void *heap_init_base;
static uintptr_t heap_ceiling;

void heapblock_init(void)
{
    void *top_of_kernel_data = (void *)cur_boot_args.top_of_kernel_data;

    heap_base = top_of_kernel_data;
    heapblock_alloc(0); // align base
    heap_init_base = heap_base;

    printf("Heap base: %p\n", heap_base);
}

void *heapblock_alloc(size_t size)
{
    return heapblock_alloc_aligned(size, 64);
}

void *heapblock_alloc_aligned(size_t size, size_t align)
{
    assert((align & (align - 1)) == 0);
    assert(heap_base);

    uintptr_t base = (uintptr_t)heap_base;
    if (base > UINTPTR_MAX - (align - 1))
        panic("heapblock: alignment overflow\n");
    uintptr_t block = (base + align - 1) & ~(align - 1);
    if (!heapblock_block_fits(block, size, heap_ceiling))
        panic("heapblock: 0x%lx+0x%lx crosses ceiling 0x%lx\n", block, size, heap_ceiling);
    heap_base = (void *)(block + size);

    return (void *)block;
}

bool heapblock_set_ceiling(void *ceiling)
{
    uintptr_t c = (uintptr_t)ceiling;
    if (heap_ceiling || !c || !heap_base || c <= (uintptr_t)heap_base)
        return false;
    heap_ceiling = c;
    return true;
}

void *heapblock_get_ceiling(void)
{
    return (void *)heap_ceiling;
}

void *heapblock_get_init_base(void)
{
    return heap_init_base;
}
