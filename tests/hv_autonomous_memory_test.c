#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/hv_autonomous_memory.h"

int main(void)
{
    uint64_t end = 0;

    assert(hv_autonomous_resolve_ram_end(0x850000000ULL, 0xa00000000ULL, 0x800000000ULL,
                                         0x1df708000ULL, &end));
    assert(end == 0x9df708000ULL);
    assert(end - 0x850000000ULL == 0x18f708000ULL);

    assert(hv_autonomous_resolve_ram_end(0x850000000ULL, 0x900000000ULL, 0x800000000ULL,
                                         0x1df708000ULL, &end));
    assert(end == 0x900000000ULL);

    assert(!hv_autonomous_resolve_ram_end(0x850000000ULL, UINT64_MAX, UINT64_MAX - 0xfff, 0x1000,
                                          &end));
    assert(!hv_autonomous_resolve_ram_end(0x850000000ULL, 0x850000000ULL, 0x800000000ULL,
                                          0x1df708000ULL, &end));
    assert(!hv_autonomous_resolve_ram_end(0x850000000ULL, 0xa00000000ULL, 0x800000000ULL,
                                          0x1df708000ULL, NULL));

    puts("hv_autonomous_memory_test: ok");
    return 0;
}
