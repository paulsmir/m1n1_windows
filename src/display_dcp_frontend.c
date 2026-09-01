/* SPDX-License-Identifier: MIT */

#include "display_dcp_frontend.h"

enum display_dcp_frontend display_dcp_frontend_select(bool full_owner,
                                                       bool external)
{
    if (!full_owner)
        return DISPLAY_DCP_FRONTEND_IBOOT;
    if (external)
        return DISPLAY_DCP_FRONTEND_UNSUPPORTED;
    return DISPLAY_DCP_FRONTEND_IOMFB;
}

bool display_dcp_frontend_has_latch_source(enum display_dcp_frontend frontend,
                                           bool owner_active)
{
    return frontend == DISPLAY_DCP_FRONTEND_IOMFB && owner_active;
}
