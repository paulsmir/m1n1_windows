/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdio.h>

#include "../src/asc_tx_gate.h"

int main(void)
{
    struct asc_tx_gate gate;
    int first;
    int second;

    asc_tx_gate_init(&gate);
    assert(asc_tx_gate_try_acquire(&gate, &first));
    assert(!asc_tx_gate_try_acquire(&gate, &second));
    assert(!asc_tx_gate_release(&gate, &second));
    assert(asc_tx_gate_release(&gate, &first));
    assert(asc_tx_gate_try_acquire(&gate, &second));
    assert(asc_tx_gate_release(&gate, &second));
    puts("asc_tx_gate_test: ok");
    return 0;
}
