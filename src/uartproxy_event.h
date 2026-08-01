/* SPDX-License-Identifier: MIT */

#ifndef UARTPROXY_EVENT_H
#define UARTPROXY_EVENT_H

#ifdef UARTPROXY_EVENT_HOST_TEST
#include <stdbool.h>
#include <stdint.h>
typedef uint16_t u16;
typedef uint32_t u32;
#else
#include "types.h"
#endif

#define UARTPROXY_REQ_EVENT 0x05AA55FFu
#define UARTPROXY_CHECKSUM_SENTINEL 0xD0DECADEu

struct uartproxy_iovec {
    const void *data;
    u32 length;
};

typedef bool (*uartproxy_try_writev_fn)(void *opaque, const struct uartproxy_iovec *iov,
                                       u32 iov_count, u32 total_size);

struct uartproxy_event_backend {
    uartproxy_try_writev_fn try_writev;
    void *opaque;
    bool data_checksums_disabled;
};

bool uartproxy_event_try_sendv(const struct uartproxy_event_backend *backend, u16 event_type,
                               const void *prefix, u16 prefix_len, const void *payload,
                               u16 payload_len);

#endif
