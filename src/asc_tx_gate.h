/* SPDX-License-Identifier: MIT */

#ifndef ASC_TX_GATE_H
#define ASC_TX_GATE_H

#include <stdbool.h>
#include <stdint.h>

struct asc_tx_gate {
    uintptr_t owner;
};

void asc_tx_gate_init(struct asc_tx_gate *gate);
bool asc_tx_gate_try_acquire(struct asc_tx_gate *gate, const void *owner);
bool asc_tx_gate_release(struct asc_tx_gate *gate, const void *owner);

#endif
