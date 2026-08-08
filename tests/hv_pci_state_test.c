#include <assert.h>
#include <stdio.h>

#include "../src/hv_pci_state.h"

int main(void)
{
    struct hv_pci_state state;

    hv_pci_state_reset(0x690000000ULL, 0x400000000ULL, 64);
    assert(hv_pci_state_get(&state));
    assert(state.ecam_base == 0x690000000ULL);
    assert(state.bar_window_base == 0x400000000ULL);
    assert(state.intx_irq == 64);
    assert(!state.ecam_hooked);
    assert(!state.backend_ready);
    assert(!state.initialized);

    hv_pci_state_record_init(true, true);
    assert(hv_pci_state_get(&state));
    assert(state.ecam_hooked);
    assert(state.backend_ready);
    assert(state.initialized);

    hv_pci_state_record_config(0x6, 0x400000000ULL, true);
    assert(hv_pci_state_get(&state));
    assert(state.command == 0x6);
    assert(state.bar0_base == 0x400000000ULL);
    assert(state.bar_mapped);

    hv_pci_state_record_config(0, 0, false);
    assert(hv_pci_state_get(&state));
    assert(!state.bar_mapped);

    puts("hv_pci_state_test: ok");
    return 0;
}
