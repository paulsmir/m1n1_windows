#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/hv_fb_stream.h"
#include "../src/uartproxy_event.h"

#define DWC3_BULK_TRANSFER_SIZE 0x4000u
#define FRAME_SIZE             0x5000u
#define EVT_FRAMEBUFFER_TEST   0x100u

struct usb_sink {
    unsigned accepted_events;
    uint32_t last_event_size;
};

static uint64_t translate_identity(uint64_t ipa)
{
    return ipa;
}

static bool usb_try_writev(void *opaque, const struct uartproxy_iovec *iov,
                           uint32_t iov_count, uint32_t total_size)
{
    struct usb_sink *sink = opaque;

    (void)iov;
    (void)iov_count;
    if (total_size > DWC3_BULK_TRANSFER_SIZE)
        return false;

    sink->accepted_events++;
    sink->last_event_size = total_size;
    return true;
}

static bool send_framebuffer_event(void *opaque, const struct hv_fb_chunk_header *header,
                                   const void *payload)
{
    const struct uartproxy_event_backend backend = {
        .try_writev = usb_try_writev,
        .opaque = opaque,
        .data_checksums_disabled = true,
    };

    return uartproxy_event_try_sendv(&backend, EVT_FRAMEBUFFER_TEST, header,
                                     sizeof(*header), payload, header->payload_size);
}

int main(void)
{
    static uint8_t frame[FRAME_SIZE];
    struct hv_fb_stream stream;
    struct usb_sink sink = {0};

    assert(hv_fb_stream_configure(&stream, (uint64_t)frame, sizeof(frame), 64, 80, 256,
                                  translate_identity, send_framebuffer_event, &sink));

    hv_fb_stream_tick(&stream);

    assert(sink.accepted_events == 1);
    assert(sink.last_event_size <= DWC3_BULK_TRANSFER_SIZE);
    assert(stream.offset > 0);

    puts("hv_fb_stream_usb_limit_test: ok");
    return 0;
}
