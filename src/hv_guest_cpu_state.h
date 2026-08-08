/* SPDX-License-Identifier: MIT */

#ifndef HV_GUEST_CPU_STATE_H
#define HV_GUEST_CPU_STATE_H

#include <stdint.h>

struct hv_guest_cpu_state {
    uint64_t hacr;
    uint64_t mdcr;
    uint64_t mdscr;
    uint64_t amx_config;
    uint64_t apvmkeylo;
    uint64_t apvmkeyhi;
    uint64_t apsts;
    uint64_t actlr;
};

struct hv_guest_cpu_state hv_guest_cpu_state_prepare(uint64_t current_amx_config,
                                                      uint64_t current_actlr);

#endif
