/* SPDX-License-Identifier: MIT */

#include "hv_watchdog_snapshot.h"

void hv_watchdog_snapshot_publish(struct hv_watchdog_cpu_record *record,
                                  const struct hv_watchdog_cpu_sample *sample)
{
    if (!record || !sample)
        return;

    u32 sequence = __atomic_load_n(&record->sequence, __ATOMIC_RELAXED);

    __atomic_store_n(&record->sequence, sequence + 1, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    record->sample = *sample;
    __atomic_store_n(&record->sequence, sequence + 2, __ATOMIC_RELEASE);
}

bool hv_watchdog_snapshot_read(const struct hv_watchdog_cpu_record *record,
                               struct hv_watchdog_cpu_sample *sample)
{
    if (!record || !sample)
        return false;

    u32 before = __atomic_load_n(&record->sequence, __ATOMIC_ACQUIRE);
    if (!before || (before & 1))
        return false;

    *sample = record->sample;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    u32 after = __atomic_load_n(&record->sequence, __ATOMIC_RELAXED);

    return before == after && !(after & 1);
}

bool hv_watchdog_snapshot_due(u64 per_cpu_sample_tick)
{
    /* 64 ticks is 640 ms at the 100 Hz T8103 secondary cadence. */
    return per_cpu_sample_tick && !(per_cpu_sample_tick & 0x3f);
}

bool hv_watchdog_snapshot_dump_due(u64 current_tick, u64 previous_tick, u64 interval)
{
    return interval && current_tick - previous_tick >= interval;
}
