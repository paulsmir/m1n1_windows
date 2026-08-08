#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/hv_guest_cpu_state.h"

int main(void)
{
    const uint64_t initial_amx = 0x1234;
    const uint64_t initial_actlr = 0x55;
    struct hv_guest_cpu_state state =
        hv_guest_cpu_state_prepare(initial_amx, initial_actlr);

    /* Literal values mirror the working assisted launch contract. */
    assert(state.hacr == 0x0317000000014000ULL);
    assert(state.mdcr == 0x0000000000000f00ULL);
    assert(state.mdscr == 0x0000000000008000ULL);
    assert(state.amx_config == 0x4000000000001234ULL);
    assert(state.apvmkeylo == 0x4e7672476f6e6147ULL);
    assert(state.apvmkeyhi == 0x697665596f755570ULL);
    assert(state.apsts == 1);
    assert(state.actlr == 0x1055ULL);

    puts("hv_guest_cpu_state_test: ok");
    return 0;
}
