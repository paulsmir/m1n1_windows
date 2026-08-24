#include <assert.h>
#include <stdio.h>

#include "../src/usb_dwc3_bulk_state.h"

_Static_assert(USB_DWC3_BULK_TRANSFER_SIZE == 512,
               "CDC DWC3 submissions must stay on the proven USB max-packet boundary");

int main(void)
{
    /* A max-packet-aligned data transfer needs one terminating ZLP. */
    assert(usb_dwc3_bulk_zlp_pending_after_submit(16 * 1024));

    /* Submitting that ZLP consumes the pending state instead of re-arming it forever. */
    assert(!usb_dwc3_bulk_zlp_pending_after_submit(0));

    /* A short data transfer terminates itself. */
    assert(!usb_dwc3_bulk_zlp_pending_after_submit(511));

    /* A host short-read must retry the unsent tail before newer ring data. */
    size_t retry_offset = 0;
    assert(usb_dwc3_bulk_in_retry(16 * 1024, 4, &retry_offset));
    assert(retry_offset == 16 * 1024 - 4);

    /* A fully consumed transfer has no tail to retry. */
    assert(!usb_dwc3_bulk_in_retry(16 * 1024, 0, &retry_offset));

    /* Hardware can never report more remaining bytes than were submitted. */
    assert(!usb_dwc3_bulk_in_retry(512, 513, &retry_offset));

    puts("usb_dwc3_bulk_state_test: ok");
    return 0;
}
