/* SPDX-License-Identifier: MIT */

#ifndef HV_NVME_FAST_PATH_H
#define HV_NVME_FAST_PATH_H

#ifdef HV_NVME_FAST_PATH_HOST_TEST
#include <stdbool.h>
#include <stdint.h>
typedef uint64_t u64;
#else
#include "types.h"
#endif

bool hv_nvme_bar_contains(u64 base, u64 size, u64 ipa);

#endif
