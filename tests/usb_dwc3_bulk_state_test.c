#include <assert.h>
#include <stdio.h>

#include "../src/usb_dwc3_bulk_state.h"

int main(void)
{
    /* A max-packet-aligned data transfer needs one terminating ZLP. */
    assert(usb_dwc3_bulk_zlp_pending_after_submit(16 * 1024));

    /* Submitting that ZLP consumes the pending state instead of re-arming it forever. */
    assert(!usb_dwc3_bulk_zlp_pending_after_submit(0));

    /* A short data transfer terminates itself. */
    assert(!usb_dwc3_bulk_zlp_pending_after_submit(511));

    puts("usb_dwc3_bulk_state_test: ok");
    return 0;
}
