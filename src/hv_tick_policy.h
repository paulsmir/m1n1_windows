// SPDX-License-Identifier: MIT
#ifndef HV_TICK_POLICY_H
#define HV_TICK_POLICY_H

#include <stdbool.h>
#include <stdint.h>

uint32_t hv_secondary_tick_rate(bool has_ecv);
uint64_t hv_tick_interval_ticks(uint64_t counter_frequency, uint32_t tick_rate);

#endif
