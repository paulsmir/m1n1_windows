/* SPDX-License-Identifier: MIT */

#include "hv_fiq_fast_path.h"

bool hv_fiq_secondary_fast_eligible(int cpu, int interruptible_cpu, int want_cpu,
                                    bool host_rendezvous_active)
{
    return cpu != interruptible_cpu && want_cpu == -1 && !host_rendezvous_active;
}

bool hv_fiq_secondary_fast_complete(bool eligible,
                                    bool physical_fiq_pending,
                                    bool virtual_irq_pending)
{
    return eligible && !physical_fiq_pending && !virtual_irq_pending;
}
