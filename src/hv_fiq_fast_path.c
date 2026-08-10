/* SPDX-License-Identifier: MIT */

#include "hv_fiq_fast_path.h"

bool hv_fiq_secondary_fast_eligible(int cpu, int interruptible_cpu, int want_cpu)
{
    return cpu != interruptible_cpu && want_cpu == -1;
}

bool hv_fiq_secondary_fast_complete(bool eligible, bool fiq_pending_after_local_sources)
{
    return eligible && !fiq_pending_after_local_sources;
}
