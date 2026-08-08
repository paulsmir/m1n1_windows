/* SPDX-License-Identifier: MIT */

#ifndef HV_LAUNCH_TRANSPORT_H
#define HV_LAUNCH_TRANSPORT_H

#include "hv_launch_contract.h"

#define HV_LAUNCH_FRAME_MAGIC      "J313CONTRACT"
#define HV_LAUNCH_FRAME_MAGIC_SIZE 12
#define HV_LAUNCH_FRAME_VERSION    1

struct hv_launch_frame_header {
    uint8_t magic[HV_LAUNCH_FRAME_MAGIC_SIZE];
    uint16_t version;
    uint16_t header_size;
    uint32_t payload_size;
    uint32_t checkpoint;
    uint32_t sequence;
    uint32_t payload_crc32;
} __attribute__((packed));

_Static_assert(sizeof(struct hv_launch_frame_header) == 32, "launch frame header size");

enum hv_launch_transport_result {
    HV_LAUNCH_TRANSPORT_ERROR = -1,
    HV_LAUNCH_TRANSPORT_PENDING,
    HV_LAUNCH_TRANSPORT_COMPLETE,
};

typedef size_t (*hv_launch_transport_sink_fn)(void *opaque, const void *data, size_t size);

struct hv_launch_transport {
    struct hv_launch_frame_header header;
    const struct hv_contract_snapshot *snapshot;
    size_t offset;
};

bool hv_launch_transport_begin(struct hv_launch_transport *transport,
                               const struct hv_contract_snapshot *snapshot);
enum hv_launch_transport_result hv_launch_transport_pump(struct hv_launch_transport *transport,
                                                         hv_launch_transport_sink_fn sink,
                                                         void *opaque);

#endif
