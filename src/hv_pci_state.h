/* SPDX-License-Identifier: MIT */

#ifndef HV_PCI_STATE_H
#define HV_PCI_STATE_H

#include <stdbool.h>
#include <stdint.h>

struct hv_pci_state {
    uint64_t ecam_base;
    uint64_t bar_window_base;
    uint64_t bar0_base;
    uint32_t intx_irq;
    uint16_t command;
    bool ecam_hooked;
    bool backend_ready;
    bool initialized;
    bool bar_mapped;
};

void hv_pci_state_reset(uint64_t ecam_base, uint64_t bar_window_base, uint32_t intx_irq);
void hv_pci_state_record_init(bool ecam_hooked, bool backend_ready);
void hv_pci_state_record_config(uint16_t command, uint64_t bar0_base, bool bar_mapped);
bool hv_pci_state_get(struct hv_pci_state *out);

#endif
