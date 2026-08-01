/* SPDX-License-Identifier: MIT */

#include "hv_fb_stream.h"

#ifdef HV_FB_STREAM_HOST_TEST
#include <stdint.h>
#include <string.h>
#else
#include "string.h"
#include "types.h"
#endif

bool hv_fb_stream_configure(struct hv_fb_stream *stream, u64 ipa, u64 total_size, u32 width,
                            u32 height, u32 stride, hv_fb_translate_fn translate,
                            hv_fb_send_fn send, void *send_opaque)
{
    if (!stream)
        return false;

    memset(stream, 0, sizeof(*stream));

    if (!ipa || !total_size || !width || !height || !stride || !translate || !send)
        return false;
    if (width > 0xffff || height > 0xffff || stride < width * 4ULL)
        return false;
    if (total_size > 0xffffffffULL || (u64)stride * height != total_size)
        return false;
    if (ipa > ~0ULL - (total_size - 1))
        return false;

    u64 pa = translate(ipa);
    u64 last_pa = translate(ipa + total_size - 1);
    if (!pa || !last_pa || last_pa - pa != total_size - 1)
        return false;

    stream->enabled = true;
    stream->ipa = ipa;
    stream->pa = pa;
    stream->total_size = (u32)total_size;
    stream->width = (u16)width;
    stream->height = (u16)height;
    stream->stride = stride;
    stream->send = send;
    stream->send_opaque = send_opaque;
    return true;
}

void hv_fb_stream_disable(struct hv_fb_stream *stream)
{
    if (stream)
        stream->enabled = false;
}

void hv_fb_stream_tick(struct hv_fb_stream *stream)
{
    if (!stream || !stream->enabled)
        return;

    for (u32 i = 0; i < HV_FB_STREAM_CHUNKS_PER_TICK; i++) {
        u32 remaining = stream->total_size - stream->offset;
        u32 payload_size = remaining < HV_FB_STREAM_PAYLOAD_SIZE
                               ? remaining
                               : HV_FB_STREAM_PAYLOAD_SIZE;
        struct hv_fb_chunk_header header = {
            .magic = HV_FB_STREAM_MAGIC,
            .frame_id = stream->frame_id,
            .offset = stream->offset,
            .total_size = stream->total_size,
            .width = stream->width,
            .height = stream->height,
            .stride = stream->stride,
            .payload_size = payload_size,
        };
        const void *payload = (const void *)(uintptr_t)(stream->pa + stream->offset);

        if (!stream->send(stream->send_opaque, &header, payload)) {
            stream->stats.backpressure_skips++;
            break;
        }

        stream->stats.queued_chunks++;
        stream->offset += payload_size;
        if (stream->offset == stream->total_size) {
            stream->stats.completed_frames++;
            stream->frame_id++;
            stream->offset = 0;
        }
    }
}

struct hv_fb_stream_stats hv_fb_stream_get_stats(const struct hv_fb_stream *stream)
{
    if (!stream)
        return (struct hv_fb_stream_stats){0};
    return stream->stats;
}
