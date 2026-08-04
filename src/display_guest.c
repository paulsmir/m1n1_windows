/* SPDX-License-Identifier: MIT */

#include "display_guest.h"

#define DISPLAY_GUEST_ALIGNMENT UINT64_C(0x4000)
#define DISPLAY_GUEST_DEPTH 32u
#define DISPLAY_GUEST_BYTES_PER_PIXEL 4u

bool display_guest_validate(uint64_t base, uint64_t size, uint32_t width, uint32_t height,
                            uint32_t stride, uint32_t depth)
{
    if (!base || !size || !width || !height || depth != DISPLAY_GUEST_DEPTH)
        return false;
    if ((base | size) & (DISPLAY_GUEST_ALIGNMENT - 1))
        return false;
    if ((uint64_t)stride < (uint64_t)width * DISPLAY_GUEST_BYTES_PER_PIXEL)
        return false;

    uint64_t required = (uint64_t)stride * height;
    if (required != size)
        return false;
    if (base > UINT64_MAX - (size - 1))
        return false;

    return true;
}

bool display_guest_prepare(uint64_t base, uint64_t size, uint32_t width, uint32_t height,
                           uint32_t stride, uint32_t depth,
                           const struct display_guest_ops *ops, void *opaque,
                           uint64_t *out_iova)
{
    if (!ops || !ops->map || !ops->present || !ops->unmap || !out_iova)
        return false;

    *out_iova = 0;
    if (!display_guest_validate(base, size, width, height, stride, depth))
        return false;

    uint64_t iova = ops->map(opaque, base, size);
    if (!iova)
        return false;

    if (!ops->present(opaque, iova, width, height, stride)) {
        ops->unmap(opaque, iova, size);
        return false;
    }

    *out_iova = iova;
    return true;
}

bool display_guest_fit(uint32_t source_width, uint32_t source_height, uint32_t panel_width,
                       uint32_t panel_height, struct display_guest_rect *destination)
{
    if (!source_width || !source_height || !panel_width || !panel_height || !destination)
        return false;

    if ((uint64_t)panel_width * source_height <= (uint64_t)panel_height * source_width) {
        destination->width = panel_width;
        destination->height = (uint64_t)panel_width * source_height / source_width;
    } else {
        destination->height = panel_height;
        destination->width = (uint64_t)panel_height * source_width / source_height;
    }
    destination->x = (panel_width - destination->width) / 2;
    destination->y = (panel_height - destination->height) / 2;
    return destination->width && destination->height;
}
