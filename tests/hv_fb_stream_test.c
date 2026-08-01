#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/hv_fb_stream.h"

struct sent_event {
    struct hv_fb_chunk_header header;
    uint8_t payload[HV_FB_STREAM_PAYLOAD_SIZE];
};

struct fake_sender {
    bool accept;
    unsigned count;
    struct sent_event events[8];
};

static uint64_t translate_identity(uint64_t ipa)
{
    return ipa;
}

static uint64_t translate_none(uint64_t ipa)
{
    (void)ipa;
    return 0;
}

static uint64_t discontinuous_base;

static uint64_t translate_discontinuous(uint64_t ipa)
{
    if (ipa == discontinuous_base)
        return ipa;
    return ipa + 1;
}

static bool fake_send(void *opaque, const struct hv_fb_chunk_header *header,
                      const void *payload)
{
    struct fake_sender *sender = opaque;

    if (!sender->accept)
        return false;

    assert(sender->count < 8);
    assert(header->payload_size <= sizeof(sender->events[0].payload));
    sender->events[sender->count].header = *header;
    memcpy(sender->events[sender->count].payload, payload, header->payload_size);
    sender->count++;
    return true;
}

int main(void)
{
    uint8_t frame[11];
    struct hv_fb_stream stream;
    struct fake_sender sender = {.accept = true};

    for (unsigned i = 0; i < sizeof(frame); i++)
        frame[i] = (uint8_t)(0x30 + i);

    assert(hv_fb_stream_configure(&stream, (uint64_t)frame, sizeof(frame), 1, 1, 11,
                                  translate_identity, fake_send, &sender));

    hv_fb_stream_tick(&stream);
    assert(sender.count == 2);
    assert(sender.events[0].header.magic == HV_FB_STREAM_MAGIC);
    assert(sender.events[0].header.frame_id == 0);
    assert(sender.events[0].header.offset == 0);
    assert(sender.events[0].header.payload_size == 4);
    assert(memcmp(sender.events[0].payload, frame, 4) == 0);
    assert(sender.events[1].header.frame_id == 0);
    assert(sender.events[1].header.offset == 4);
    assert(sender.events[1].header.payload_size == 4);
    assert(memcmp(sender.events[1].payload, frame + 4, 4) == 0);
    assert(stream.offset == 8);

    sender.accept = false;
    hv_fb_stream_tick(&stream);
    assert(sender.count == 2);
    assert(stream.offset == 8);
    assert(hv_fb_stream_get_stats(&stream).backpressure_skips == 1);

    sender.accept = true;
    hv_fb_stream_tick(&stream);
    assert(sender.count == 4);
    assert(sender.events[2].header.frame_id == 0);
    assert(sender.events[2].header.offset == 8);
    assert(sender.events[2].header.payload_size == 3);
    assert(memcmp(sender.events[2].payload, frame + 8, 3) == 0);
    assert(sender.events[3].header.frame_id == 1);
    assert(sender.events[3].header.offset == 0);
    assert(sender.events[3].header.payload_size == 4);
    assert(stream.frame_id == 1);
    assert(stream.offset == 4);

    assert(!hv_fb_stream_configure(&stream, (uint64_t)frame, 0, 1, 1, 4,
                                   translate_identity, fake_send, &sender));
    assert(!hv_fb_stream_configure(&stream, (uint64_t)frame, 4, 0, 1, 4,
                                   translate_identity, fake_send, &sender));
    assert(!hv_fb_stream_configure(&stream, (uint64_t)frame, 4, 1, 0, 4,
                                   translate_identity, fake_send, &sender));
    assert(!hv_fb_stream_configure(&stream, (uint64_t)frame, 3, 1, 1, 3,
                                   translate_identity, fake_send, &sender));
    assert(!hv_fb_stream_configure(&stream, (uint64_t)frame, 10, 1, 1, 11,
                                   translate_identity, fake_send, &sender));
    assert(!hv_fb_stream_configure(&stream, (uint64_t)frame, 4, 1, 1, 4,
                                   translate_none, fake_send, &sender));

    discontinuous_base = (uint64_t)frame;
    assert(!hv_fb_stream_configure(&stream, (uint64_t)frame, 4, 1, 1, 4,
                                   translate_discontinuous, fake_send, &sender));

    assert(hv_fb_stream_configure(&stream, (uint64_t)frame, 4, 1, 1, 4,
                                  translate_identity, fake_send, &sender));
    hv_fb_stream_disable(&stream);
    assert(!stream.enabled);
    sender.count = 0;
    hv_fb_stream_tick(&stream);
    assert(sender.count == 0);

    puts("hv_fb_stream_test: ok");
    return 0;
}
