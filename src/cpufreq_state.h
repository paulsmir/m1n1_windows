/* SPDX-License-Identifier: MIT */

#ifndef CPUFREQ_STATE_H
#define CPUFREQ_STATE_H

#include <stdbool.h>
#include <stdint.h>

bool cpufreq_pstate_supported(uint32_t soc_id);
uint32_t cpufreq_decode_pstate(uint32_t soc_id, uint64_t value);

#endif
