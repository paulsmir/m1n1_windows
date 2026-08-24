/* SPDX-License-Identifier: MIT */

#include "usb_dwc3_bulk_state.h"

bool usb_dwc3_bulk_zlp_pending_after_submit(size_t payload_size)
{
    return payload_size != 0 && payload_size % 512 == 0;
}

bool usb_dwc3_bulk_in_retry(size_t submitted, size_t remaining, size_t *retry_offset)
{
    if (!retry_offset || !remaining || remaining > submitted)
        return false;

    *retry_offset = submitted - remaining;
    return true;
}
