#include <assert.h>
#include <stdio.h>

#include "../src/hv_runtime_diag.h"

int main(void)
{
    unsigned long counter = 41;

    assert(hv_runtime_diag_enabled());
    assert(!hv_runtime_trace_enabled());
    assert(!hv_runtime_diag_verbose_enabled());
    HV_RUNTIME_DIAG_COUNT(counter);
    assert(counter == 42);
    puts("hv_runtime_diag_debug_test: ok");
    return 0;
}
