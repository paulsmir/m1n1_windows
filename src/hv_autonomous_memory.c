/* SPDX-License-Identifier: MIT */

#include "hv_autonomous_memory.h"

bool hv_autonomous_resolve_ram_end(uint64_t guest_base, uint64_t configured_end,
                                   uint64_t platform_base, uint64_t platform_size,
                                   uint64_t *effective_end)
{
    uint64_t platform_end;
    uint64_t resolved;

    if (!effective_end || platform_size > UINT64_MAX - platform_base)
        return false;

    platform_end = platform_base + platform_size;
    resolved = configured_end < platform_end ? configured_end : platform_end;
    if (resolved <= guest_base)
        return false;

    *effective_end = resolved;
    return true;
}
