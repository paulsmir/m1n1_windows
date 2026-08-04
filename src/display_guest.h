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
bool display_guest_fit(uint32_t source_width, uint32_t source_height, uint32_t panel_width,
                       uint32_t panel_height, struct display_guest_rect *destination);

#endif
