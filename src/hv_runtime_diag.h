/* SPDX-License-Identifier: MIT */

#ifndef HV_RUNTIME_DIAG_H
#define HV_RUNTIME_DIAG_H

#ifdef HV_RUNTIME_DIAG_HOST_TEST
#include <stdbool.h>
#else
#include "../build/build_cfg.h"
#include "types.h"
#endif

/* RELEASE removes periodic formatting and console I/O from hot guest paths. */
static inline bool hv_runtime_diag_enabled(void)
{
#ifdef RELEASE
    return false;
#else
    return true;
#endif
}

#endif
