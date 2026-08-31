/* SPDX-License-Identifier: MIT */

#ifndef DCP_IOMFB_V13_5_ABI_H
#define DCP_IOMFB_V13_5_ABI_H

#ifdef DCP_IOMFB_V13_5_ABI_HOST_TEST
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#else
#include "types.h"
#endif

struct dcp_iomfb_abi_size {
    uint32_t input;
    uint32_t output;
};

bool dcp_iomfb_v13_5_callback_size(unsigned int id,
                                    struct dcp_iomfb_abi_size *size);
bool dcp_iomfb_v13_5_method_size(unsigned int id,
                                 struct dcp_iomfb_abi_size *size);

#endif
