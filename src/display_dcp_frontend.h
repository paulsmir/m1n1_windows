/* SPDX-License-Identifier: MIT */
#ifndef DISPLAY_DCP_FRONTEND_H
#define DISPLAY_DCP_FRONTEND_H

#include <stdbool.h>

enum display_dcp_frontend {
    DISPLAY_DCP_FRONTEND_UNSUPPORTED = 0,
    DISPLAY_DCP_FRONTEND_IBOOT,
    DISPLAY_DCP_FRONTEND_IOMFB,
};

enum display_dcp_frontend display_dcp_frontend_select(bool full_owner,
                                                       bool external);
bool display_dcp_frontend_has_latch_source(enum display_dcp_frontend frontend,
                                           bool owner_active);

#endif
