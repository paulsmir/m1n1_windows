/* SPDX-License-Identifier: MIT */
#ifndef DCP_IOMFB_OWNER_LIFECYCLE_H
#define DCP_IOMFB_OWNER_LIFECYCLE_H

#include <stdbool.h>

typedef bool (*dcp_iomfb_owner_stage_fn)(void *opaque);

bool dcp_iomfb_owner_start_ordered(void *opaque,
                                   dcp_iomfb_owner_stage_fn start_system,
                                   dcp_iomfb_owner_stage_fn start_iomfb);

#endif
