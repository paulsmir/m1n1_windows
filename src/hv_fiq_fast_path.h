/* SPDX-License-Identifier: MIT */

#ifndef HV_FIQ_FAST_PATH_H
#define HV_FIQ_FAST_PATH_H

#ifdef HV_FIQ_FAST_PATH_HOST_TEST
#include <stdbool.h>
#else
#include "types.h"
#endif

/*
 * Secondary vCPUs must service their per-CPU timer and IPI sources without
 * waiting for the legacy, global hypervisor lock.  The interruptible CPU and
 * explicit proxy CPU switches still use the serialized path.
 */
bool hv_fiq_secondary_fast_eligible(int cpu, int interruptible_cpu, int want_cpu,
                                    bool host_rendezvous_active);
bool hv_fiq_secondary_fast_complete(bool eligible, bool fiq_pending_after_local_sources,
                                    bool virtual_irq_pending);

#endif
