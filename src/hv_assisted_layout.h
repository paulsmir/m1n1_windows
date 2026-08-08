/* SPDX-License-Identifier: MIT */

#ifndef HV_ASSISTED_LAYOUT_H
#define HV_ASSISTED_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HV_ASSISTED_GUEST_OFFSET  (16ULL * 1024ULL * 1024ULL)
#define HV_ASSISTED_ALIGNMENT     0x4000ULL
#define HV_ASSISTED_BOOTARGS_SIZE 0x4000ULL

struct hv_assisted_layout {
    uint64_t adt_base;
    uint64_t trust_cache_base;
    uint64_t firmware_base;
    uint64_t sepfw_base;
    uint64_t preoslog_base;
    uint64_t boot_args_base;
    uint64_t top_of_kernel_data;
};

bool hv_assisted_layout_compute(uint64_t phys_base, size_t adt_size,
                                size_t trust_cache_size, size_t firmware_size,
                                size_t sepfw_size, size_t preoslog_size,
                                struct hv_assisted_layout *out);

#endif
