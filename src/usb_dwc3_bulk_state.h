/* SPDX-License-Identifier: MIT */

#ifndef USB_DWC3_BULK_STATE_H
#define USB_DWC3_BULK_STATE_H

#ifdef USB_DWC3_BULK_STATE_HOST_TEST
#include <stdbool.h>
#include <stddef.h>
#else
#include "types.h"
#endif

bool usb_dwc3_bulk_zlp_pending_after_submit(size_t payload_size);

#endif
