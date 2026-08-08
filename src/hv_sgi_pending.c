/* SPDX-License-Identifier: MIT */

#include "hv_sgi_pending.h"

u32 hv_sgi_ack_and_take_pending(u32 *pending_mask, hv_sgi_ack_fn acknowledge, void *opaque)
{
    acknowledge(opaque);
    return __atomic_exchange_n(pending_mask, 0, __ATOMIC_ACQ_REL);
}
