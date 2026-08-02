/* SPDX-License-Identifier: MIT */

#include "usb_dwc3_bulk_state.h"

bool usb_dwc3_bulk_zlp_pending_after_submit(size_t payload_size)
{
    return payload_size != 0 && payload_size % 512 == 0;
}
