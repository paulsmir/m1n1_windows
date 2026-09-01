/* SPDX-License-Identifier: MIT */

#ifndef DISPLAY_GUEST_H
#define DISPLAY_GUEST_H

#include <stdbool.h>
#include <stdint.h>

struct display_guest_ops {
    uint64_t (*map)(void *opaque, uint64_t base, uint64_t size);
    bool (*present)(void *opaque, uint64_t iova, uint32_t width, uint32_t height,
                    uint32_t stride);
    void (*unmap)(void *opaque, uint64_t iova, uint64_t size);
};

struct display_guest_owner_ops {
    uint64_t (*map)(void *opaque, uint64_t base, uint64_t size);
    uint32_t (*present_begin)(void *opaque, uint64_t iova, uint32_t width,
                              uint32_t height, uint32_t stride);
    int (*latch_poll)(void *opaque, uint32_t expected_swap_id);
    void (*wait)(void *opaque);
};

enum display_guest_owner_result {
    DISPLAY_GUEST_OWNER_REJECTED = 0,
    DISPLAY_GUEST_OWNER_LATCHED,
    DISPLAY_GUEST_OWNER_UNCERTAIN,
};

struct display_guest_rect {
    uint32_t width;
    uint32_t height;
    uint32_t x;
    uint32_t y;
};

bool display_guest_validate(uint64_t base, uint64_t size, uint32_t width, uint32_t height,
                            uint32_t stride, uint32_t depth);
bool display_guest_prepare(uint64_t base, uint64_t size, uint32_t width, uint32_t height,
                           uint32_t stride, uint32_t depth,
                           const struct display_guest_ops *ops, void *opaque,
                           uint64_t *out_iova);
enum display_guest_owner_result display_guest_prepare_owner(
    uint64_t base, uint64_t size, uint32_t width, uint32_t height,
    uint32_t stride, uint32_t depth,
    const struct display_guest_owner_ops *ops, void *opaque,
    unsigned int latch_attempts, uint64_t *out_iova);
bool display_guest_fit(uint32_t source_width, uint32_t source_height, uint32_t panel_width,
                       uint32_t panel_height, struct display_guest_rect *destination);

#endif
