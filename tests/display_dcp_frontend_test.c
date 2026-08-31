#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "../src/display_dcp_frontend.h"

int main(void)
{
    assert(display_dcp_frontend_select(false, false) ==
           DISPLAY_DCP_FRONTEND_IBOOT);
    assert(display_dcp_frontend_select(false, true) ==
           DISPLAY_DCP_FRONTEND_IBOOT);
    assert(display_dcp_frontend_select(true, false) ==
           DISPLAY_DCP_FRONTEND_IOMFB);
    assert(display_dcp_frontend_select(true, true) ==
           DISPLAY_DCP_FRONTEND_UNSUPPORTED);

    puts("display_dcp_frontend_test: ok");
    return 0;
}
