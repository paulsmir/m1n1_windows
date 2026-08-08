/* SPDX-License-Identifier: MIT */

#include "hv_assisted_layout.h"

#include <limits.h>

static bool add_u64(uint64_t left, uint64_t right, uint64_t *out)
{
    if (left > UINT64_MAX - right)
        return false;
    *out = left + right;
    return true;
}

static bool align_u64(uint64_t value, uint64_t *out)
{
    uint64_t remainder = value & (HV_ASSISTED_ALIGNMENT - 1);
    return !remainder ? (*out = value, true)
                      : add_u64(value, HV_ASSISTED_ALIGNMENT - remainder, out);
}

static bool advance_aligned(uint64_t base, size_t size, uint64_t *next)
{
    uint64_t end;
    return add_u64(base, size, &end) && align_u64(end, next);
}

bool hv_assisted_layout_compute(uint64_t phys_base, size_t adt_size,
                                size_t trust_cache_size, size_t firmware_size,
                                size_t sepfw_size, size_t preoslog_size,
                                struct hv_assisted_layout *out)
{
    uint64_t cursor;

    if (!out || !add_u64(phys_base, HV_ASSISTED_GUEST_OFFSET, &cursor))
        return false;
    out->adt_base = cursor;
    if (!advance_aligned(cursor, adt_size, &cursor))
        return false;
    out->trust_cache_base = cursor;
    if (!advance_aligned(cursor, trust_cache_size, &cursor))
        return false;
    out->firmware_base = cursor;
    if (!advance_aligned(cursor, firmware_size, &cursor))
        return false;
    out->sepfw_base = cursor;
    if (!advance_aligned(cursor, sepfw_size, &cursor))
        return false;
    out->preoslog_base = cursor;
    if (!add_u64(cursor, preoslog_size, &cursor))
        return false;
    out->boot_args_base = cursor;
    return add_u64(cursor, HV_ASSISTED_BOOTARGS_SIZE, &out->top_of_kernel_data);
}
