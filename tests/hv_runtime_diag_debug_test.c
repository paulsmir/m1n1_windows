#include <assert.h>
#include <stdio.h>

#include "../src/hv_runtime_diag.h"

int main(void)
{
    assert(hv_runtime_diag_enabled());
    puts("hv_runtime_diag_debug_test: ok");
    return 0;
}
