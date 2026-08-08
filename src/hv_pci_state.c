/* SPDX-License-Identifier: MIT */

#include "hv_pci_state.h"

static struct hv_pci_state observed;
static bool observed_valid;

void hv_pci_state_reset(uint64_t ecam_base, uint64_t bar_window_base, uint32_t intx_irq)
{
    observed = (struct hv_pci_state){
        .ecam_base = ecam_base,
        .bar_window_base = bar_window_base,
        .intx_irq = intx_irq,
    };
    observed_valid = true;
}

void hv_pci_state_record_init(bool ecam_hooked, bool backend_ready)
{
    if (!observed_valid)
        return;

    observed.ecam_hooked = ecam_hooked;
    observed.backend_ready = backend_ready;
    observed.initialized = ecam_hooked && backend_ready;
}

void hv_pci_state_record_config(uint16_t command, uint64_t bar0_base, bool bar_mapped)
{
    if (!observed_valid)
        return;

    observed.command = command;
    observed.bar0_base = bar0_base;
    observed.bar_mapped = bar_mapped;
}

bool hv_pci_state_get(struct hv_pci_state *out)
{
    if (!out || !observed_valid)
        return false;

    *out = observed;
    return true;
}
