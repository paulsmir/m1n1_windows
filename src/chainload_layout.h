/* SPDX-License-Identifier: MIT */

#ifndef CHAINLOAD_LAYOUT_H
#define CHAINLOAD_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>

#define CHAINLOAD_LAYOUT_ALIGNMENT 0x4000u
#define CHAINLOAD_BOOTARGS_SIZE 0x4000u

struct chainload_layout {
    size_t sepfw_offset;
    size_t preoslog_offset;
    size_t bootargs_offset;
    size_t stub_offset;
    size_t copy_size;
    size_t allocation_size;
};

bool chainload_layout_compute(size_t image_and_vars, size_t sepfw_size,
                              size_t preoslog_size, size_t stub_size,
                              struct chainload_layout *out);

#endif
