/* SPDX-License-Identifier: MIT */

#ifndef USB_DWC3_BULK_STATE_H
#define USB_DWC3_BULK_STATE_H

#ifdef USB_DWC3_BULK_STATE_HOST_TEST
#include <stdbool.h>
#include <stddef.h>
#else
#include "types.h"
#endif

/*
 * Keep every hardware CDC submission on one USB 2.0 high-speed max-packet
 * boundary.  The original DWC3 implementation used 512 bytes; aggregating up
 * to 16 KiB in one TRB is faster, but sustained framed proxy traffic on J313
 * has repeatedly lost bytes on that path.
 */
#define USB_DWC3_BULK_TRANSFER_SIZE 512u

bool usb_dwc3_bulk_zlp_pending_after_submit(size_t payload_size);
bool usb_dwc3_bulk_in_retry(size_t submitted, size_t remaining, size_t *retry_offset);

#endif
