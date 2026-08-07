/* SPDX-License-Identifier: MIT */

#include "chainload_layout.h"

#include <stdint.h>

static bool checked_add(size_t left, size_t right, size_t *out)
{
    if (left > SIZE_MAX - right)
        return false;
    *out = left + right;
    return true;
}

static bool checked_align(size_t value, size_t *out)
{
    size_t remainder = value & (CHAINLOAD_LAYOUT_ALIGNMENT - 1);

    if (!remainder) {
        *out = value;
        return true;
    }
    return checked_add(value, CHAINLOAD_LAYOUT_ALIGNMENT - remainder, out);
}

bool chainload_layout_compute(size_t image_and_vars, size_t sepfw_size,
                              size_t preoslog_size, size_t stub_size,
                              struct chainload_layout *out)
{
    size_t cursor;

    if (!out || !checked_align(image_and_vars, &cursor))
        return false;

    out->sepfw_offset = cursor;
    if (!checked_add(cursor, sepfw_size, &cursor) || !checked_align(cursor, &cursor))
        return false;

    out->preoslog_offset = cursor;
    if (!checked_add(cursor, preoslog_size, &cursor) || !checked_align(cursor, &cursor))
        return false;

    out->bootargs_offset = cursor;
    if (!checked_add(cursor, CHAINLOAD_BOOTARGS_SIZE, &cursor))
        return false;

    out->stub_offset = cursor;
    out->copy_size = cursor;
    if (!checked_add(cursor, stub_size, &out->allocation_size))
        return false;

    return true;
}
