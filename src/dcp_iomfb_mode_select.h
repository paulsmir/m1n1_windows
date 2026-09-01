/* SPDX-License-Identifier: MIT */

#ifndef DCP_IOMFB_MODE_SELECT_H
#define DCP_IOMFB_MODE_SELECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct dcp_iomfb_mode_choice {
    uint32_t color_mode_id;
    uint32_t timing_mode_id;
};

/*
 * Select the same fixed-panel modes as the canonical m1n1 DCP client:
 * highest-score non-virtual timing and highest-score non-virtual 8-bit color.
 * The property blobs use Apple's binary OSSerialize format.
 */
bool dcp_iomfb_select_modes(const void *color_blob, size_t color_size,
                            const void *timing_blob, size_t timing_size,
                            struct dcp_iomfb_mode_choice *choice);

#endif
