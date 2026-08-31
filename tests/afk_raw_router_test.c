/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdio.h>

#include "../src/afk_raw_router.h"

struct capture {
    unsigned int calls;
    afk_raw_u8 endpoint;
    afk_raw_u64 message;
};

static int capture_message(void *opaque, afk_raw_u8 endpoint, afk_raw_u64 message)
{
    struct capture *capture = opaque;

    capture->calls++;
    capture->endpoint = endpoint;
    capture->message = message;
    return 7;
}

int main(void)
{
    struct afk_raw_router router;
    struct capture capture = {0};
    afk_raw_router_init(&router);
    assert(afk_raw_router_dispatch(&router, 0x37, 0x12345678) == AFK_RAW_NOT_HANDLED);

    assert(afk_raw_router_register(&router, 0x37, capture_message, &capture));
    assert(!afk_raw_router_register(&router, 0x37, capture_message, &capture));
    assert(afk_raw_router_dispatch(&router, 0x23, 0xabcdef) == AFK_RAW_NOT_HANDLED);
    assert(afk_raw_router_dispatch(&router, 0x37, 0x12345678) == 7);
    assert(capture.calls == 1);
    assert(capture.endpoint == 0x37);
    assert(capture.message == 0x12345678);

    assert(!afk_raw_router_unregister(&router, 0x36, capture_message, &capture));
    assert(afk_raw_router_unregister(&router, 0x37, capture_message, &capture));
    assert(afk_raw_router_dispatch(&router, 0x37, 0x12345678) == AFK_RAW_NOT_HANDLED);

    puts("afk_raw_router_test: ok");
    return 0;
}
