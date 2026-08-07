/* SPDX-License-Identifier: MIT */

#ifndef HV_AUTONOMOUS_MEMORY_H
#define HV_AUTONOMOUS_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

bool hv_autonomous_resolve_ram_end(uint64_t guest_base, uint64_t configured_end,
                                   uint64_t platform_base, uint64_t platform_size,
                                   uint64_t *effective_end);

#endif
