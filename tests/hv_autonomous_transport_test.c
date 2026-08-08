#include <assert.h>
#include <stdio.h>

#include "../src/hv_autonomous_transport.h"

int main(void)
{
    assert(hv_autonomous_debug_transport(0, 2, 2) == -1);
    assert(hv_autonomous_debug_transport(2, 2, 2) == 2);
    assert(hv_autonomous_debug_transport(3, 2, 2) == 3);
    assert(hv_autonomous_debug_transport(4, 2, 2) == -1);

    puts("hv_autonomous_transport_test: ok");
    return 0;
}
