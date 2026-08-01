#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/uartproxy_event.h"

struct fake_ring {
    uint8_t bytes[128];
    size_t capacity;
    size_t used;
    unsigned calls;
};

static bool fake_try_writev(void *opaque, const struct uartproxy_iovec *iov, u32 iov_count,
                            u32 total_size)
{
    struct fake_ring *ring = opaque;

    ring->calls++;
    if (total_size > ring->capacity - ring->used)
        return false;

    size_t before = ring->used;
    for (u32 i = 0; i < iov_count; i++) {
        memcpy(ring->bytes + ring->used, iov[i].data, iov[i].length);
        ring->used += iov[i].length;
    }
    assert(ring->used - before == total_size);
    return true;
}

static uint32_t get_u32(const uint8_t *p)
{
    uint32_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static uint16_t get_u16(const uint8_t *p)
{
    uint16_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

int main(void)
{
    static const uint8_t prefix[] = {0x10, 0x11, 0x12};
    static const uint8_t payload[] = {0x20, 0x21, 0x22, 0x23, 0x24};
    struct fake_ring ring = {.capacity = 19};
    struct uartproxy_event_backend backend = {
        .try_writev = fake_try_writev,
        .opaque = &ring,
        .data_checksums_disabled = true,
    };

    /* Wire size is 8-byte event header + 8 data bytes + 4-byte checksum. */
    assert(!uartproxy_event_try_sendv(&backend, 3, prefix, sizeof(prefix), payload,
                                      sizeof(payload)));
    assert(ring.calls == 1);
    assert(ring.used == 0);

    ring.capacity = sizeof(ring.bytes);
    assert(uartproxy_event_try_sendv(&backend, 3, prefix, sizeof(prefix), payload,
                                     sizeof(payload)));
    assert(ring.calls == 2);
    assert(ring.used == 20);
    assert(get_u32(ring.bytes) == UARTPROXY_REQ_EVENT);
    assert(get_u16(ring.bytes + 4) == sizeof(prefix) + sizeof(payload));
    assert(get_u16(ring.bytes + 6) == 3);
    assert(memcmp(ring.bytes + 8, prefix, sizeof(prefix)) == 0);
    assert(memcmp(ring.bytes + 8 + sizeof(prefix), payload, sizeof(payload)) == 0);
    assert(get_u32(ring.bytes + 16) == UARTPROXY_CHECKSUM_SENTINEL);

    ring.used = 0;
    ring.calls = 0;
    backend.data_checksums_disabled = false;
    assert(uartproxy_event_try_sendv(&backend, 3, prefix, sizeof(prefix), payload,
                                     sizeof(payload)));
    assert(ring.used == 20);
    assert(get_u32(ring.bytes + 16) != UARTPROXY_CHECKSUM_SENTINEL);

    assert(!uartproxy_event_try_sendv(NULL, 3, prefix, sizeof(prefix), payload,
                                      sizeof(payload)));
    backend.try_writev = NULL;
    assert(!uartproxy_event_try_sendv(&backend, 3, prefix, sizeof(prefix), payload,
                                      sizeof(payload)));

    puts("uartproxy_event_test: ok");
    return 0;
}
