/* SPDX-License-Identifier: MIT */

#include "uartproxy_event.h"

struct uartproxy_event_header {
    u32 type;
    u16 length;
    u16 event_type;
} __attribute__((packed));

_Static_assert(sizeof(struct uartproxy_event_header) == 8, "UART proxy event header size");

static u32 checksum_add(const void *data, u32 length, u32 sum)
{
    const unsigned char *p = data;

    while (length--) {
        sum *= 31337;
        sum += (*p++) ^ 0x5a;
    }
    return sum;
}

bool uartproxy_event_try_sendv(const struct uartproxy_event_backend *backend, u16 event_type,
                               const void *prefix, u16 prefix_len, const void *payload,
                               u16 payload_len)
{
    if (!backend || !backend->try_writev)
        return false;
    if ((prefix_len && !prefix) || (payload_len && !payload))
        return false;

    u32 data_len = (u32)prefix_len + payload_len;
    if (data_len > 0xffff)
        return false;

    const struct uartproxy_event_header header = {
        .type = UARTPROXY_REQ_EVENT,
        .length = (u16)data_len,
        .event_type = event_type,
    };
    u32 checksum = UARTPROXY_CHECKSUM_SENTINEL;
    if (!backend->data_checksums_disabled) {
        checksum = checksum_add(&header, sizeof(header), 0xDEADBEEF);
        checksum = checksum_add(prefix, prefix_len, checksum);
        checksum = checksum_add(payload, payload_len, checksum);
        checksum ^= 0xADDEDBAD;
    }

    const struct uartproxy_iovec iov[] = {
        {.data = &header, .length = sizeof(header)},
        {.data = prefix, .length = prefix_len},
        {.data = payload, .length = payload_len},
        {.data = &checksum, .length = sizeof(checksum)},
    };

    return backend->try_writev(backend->opaque, iov, sizeof(iov) / sizeof(iov[0]),
                               sizeof(header) + data_len + sizeof(checksum));
}
