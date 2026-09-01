/* SPDX-License-Identifier: MIT */

#ifndef DCP_IOMFB_CLOCK_H
#define DCP_IOMFB_CLOCK_H

#ifdef DCP_IOMFB_CLOCK_HOST_TEST
#include <stdbool.h>
#include <stdint.h>
typedef uint64_t u64;
#else
#include "types.h"
#endif

struct dcp_iomfb_clock_anchor {
    u64 utc_ms;
    u64 counter;
    u64 frequency;
};

bool dcp_iomfb_clock_now(const struct dcp_iomfb_clock_anchor *anchor,
                         u64 counter, u64 frequency, u64 *utc_ms);

#endif
