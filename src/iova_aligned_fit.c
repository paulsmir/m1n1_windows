/* SPDX-License-Identifier: MIT */

#include "iova_aligned_fit.h"

bool iova_aligned_fit(iova_fit_u64 block_start, iova_fit_u64 block_size,
                      iova_fit_u64 allocation_size, iova_fit_u64 alignment,
                      iova_fit_u64 *allocation_start, iova_fit_u64 *prefix_size,
                      iova_fit_u64 *suffix_size)
{
    iova_fit_u64 aligned;
    iova_fit_u64 prefix;

    if (!allocation_start || !prefix_size || !suffix_size || !allocation_size ||
        !alignment || (alignment & (alignment - 1)) != 0)
        return false;
    if (block_start > ~(iova_fit_u64)0 - (alignment - 1))
        return false;

    aligned = (block_start + alignment - 1) & ~(alignment - 1);
    prefix = aligned - block_start;
    if (prefix > block_size || allocation_size > block_size - prefix)
        return false;

    *allocation_start = aligned;
    *prefix_size = prefix;
    *suffix_size = block_size - prefix - allocation_size;
    return true;
}
