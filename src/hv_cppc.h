/* SPDX-License-Identifier: MIT */

#ifndef HV_CPPC_H
#define HV_CPPC_H

#include "types.h"

/* Guest-physical page holding the per-CPU _CPC registers referenced by the
 * Mu DSDT (Platform/MacBookAirMid2020Pkg AcpiTables): CPU n at +n*0x40,
 * DesiredPerformance +0x00 (32-bit), Reference counter +0x08 (64-bit),
 * Delivered counter +0x10 (64-bit), PerformanceLimited +0x18 (32-bit). */
#define HV_CPPC_BASE 0x300010000ULL
#define HV_CPPC_SIZE 0x4000ULL

bool hv_cppc_map(void);

#endif
