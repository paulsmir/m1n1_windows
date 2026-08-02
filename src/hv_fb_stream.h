/* SPDX-License-Identifier: MIT */

#ifndef HV_FB_STREAM_H
#define HV_FB_STREAM_H

#ifdef HV_FB_STREAM_HOST_TEST
#include <stdbool.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include "types.h"
#endif

#define HV_FB_STREAM_MAGIC 0x31424656u

#ifndef HV_FB_STREAM_PAYLOAD_SIZE
// Keep the complete UART proxy event within one 16 KiB DWC3 transfer:
// 8-byte event header + 28-byte framebuffer header + payload + 4-byte checksum.
#define HV_FB_STREAM_PAYLOAD_SIZE 0x3fc0u
#endif

#define HV_FB_STREAM_CHUNKS_PER_TICK 1u

// hv_tick() runs at 5 kHz. A 5000-tick pause after every complete frame keeps the
// bulk USB stream around 1 fps and leaves headroom for console and KD traffic.
#ifndef HV_FB_STREAM_INTERFRAME_TICKS
#define HV_FB_STREAM_INTERFRAME_TICKS 5000u
#endif

struct hv_fb_chunk_header {
    u32 magic;
    u32 frame_id;
    u32 offset;
    u32 total_size;
    u16 width;
    u16 height;
    u32 stride;
    u32 payload_size;
} __attribute__((packed));

_Static_assert(sizeof(struct hv_fb_chunk_header) == 28, "framebuffer chunk header size");

typedef u64 (*hv_fb_translate_fn)(u64 ipa);
typedef bool (*hv_fb_send_fn)(void *opaque, const struct hv_fb_chunk_header *header,
                              const void *payload);

struct hv_fb_stream_stats {
    u64 completed_frames;
    u64 queued_chunks;
    u64 backpressure_skips;
};

struct hv_fb_stream {
    bool enabled;
    u64 ipa;
    u64 pa;
    u32 total_size;
    u16 width;
    u16 height;
    u32 stride;
    u32 frame_id;
    u32 offset;
    u32 cooldown_ticks;
    hv_fb_send_fn send;
    void *send_opaque;
    struct hv_fb_stream_stats stats;
};

bool hv_fb_stream_configure(struct hv_fb_stream *stream, u64 ipa, u64 total_size, u32 width,
                            u32 height, u32 stride, hv_fb_translate_fn translate,
                            hv_fb_send_fn send, void *send_opaque);
void hv_fb_stream_disable(struct hv_fb_stream *stream);
void hv_fb_stream_tick(struct hv_fb_stream *stream);
struct hv_fb_stream_stats hv_fb_stream_get_stats(const struct hv_fb_stream *stream);

#endif
