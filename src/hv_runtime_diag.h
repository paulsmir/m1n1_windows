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

/*
 * Continuous formatting from FIQ/vGIC hot paths is intentionally a separate
 * build-time mode.  A normal debug image keeps lock-free snapshots and
 * anomaly/bugcheck reporting enabled without synchronously flooding UART.
 */
static inline bool hv_runtime_diag_verbose_enabled(void)
{
#if defined(RELEASE) || !defined(HV_RUNTIME_DIAG_VERBOSE)
    return false;
#else
    return true;
#endif
}

#define HV_RUNTIME_TRACE(...)                                                                    \
    do {                                                                                         \
        if (hv_runtime_diag_enabled())                                                           \
            printf(__VA_ARGS__);                                                                 \
    } while (0)

#define HV_RUNTIME_VERBOSE_TRACE(...)                                                            \
    do {                                                                                         \
        if (hv_runtime_diag_verbose_enabled())                                                   \
            printf(__VA_ARGS__);                                                                 \
    } while (0)

#endif
