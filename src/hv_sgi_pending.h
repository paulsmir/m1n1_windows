/* SPDX-License-Identifier: MIT */

#ifndef HV_SGI_PENDING_H
#define HV_SGI_PENDING_H

#ifdef HV_SGI_PENDING_HOST_TEST
#include <stdint.h>
typedef uint32_t u32;
#else
#include "types.h"
#endif

typedef void (*hv_sgi_ack_fn)(void *opaque);

u32 hv_sgi_ack_and_take_pending(u32 *pending_mask, hv_sgi_ack_fn acknowledge, void *opaque);

#endif
