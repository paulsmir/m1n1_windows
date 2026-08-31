/* SPDX-License-Identifier: MIT */

#include "asc_tx_gate.h"

void asc_tx_gate_init(struct asc_tx_gate *gate)
{
    if (gate)
        __atomic_store_n(&gate->owner, 0, __ATOMIC_RELAXED);
}

bool asc_tx_gate_try_acquire(struct asc_tx_gate *gate, const void *owner)
{
    uintptr_t expected = 0;
    uintptr_t desired = (uintptr_t)owner;

    if (!gate || !owner)
        return false;
    return __atomic_compare_exchange_n(&gate->owner, &expected, desired, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

bool asc_tx_gate_release(struct asc_tx_gate *gate, const void *owner)
{
    uintptr_t expected = (uintptr_t)owner;

    if (!gate || !owner)
        return false;
    return __atomic_compare_exchange_n(&gate->owner, &expected, 0, false,
                                       __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}
