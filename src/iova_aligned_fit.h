/* SPDX-License-Identifier: MIT */

#ifndef IOVA_ALIGNED_FIT_H
#define IOVA_ALIGNED_FIT_H

#ifdef IOVA_ALIGNED_FIT_HOST_TEST
#include <stdbool.h>
#include <stdint.h>
typedef uint64_t iova_fit_u64;
#else
#include "types.h"
typedef u64 iova_fit_u64;
#endif

bool iova_aligned_fit(iova_fit_u64 block_start, iova_fit_u64 block_size,
                      iova_fit_u64 allocation_size, iova_fit_u64 alignment,
                      iova_fit_u64 *allocation_start, iova_fit_u64 *prefix_size,
                      iova_fit_u64 *suffix_size);

#endif
