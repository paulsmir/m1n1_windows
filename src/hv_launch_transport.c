/* SPDX-License-Identifier: MIT */

#include "hv_launch_transport.h"
#include "string.h"

static uint32_t frame_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = ~0U;

    while (size--) {
        crc ^= *data++;
        for (unsigned int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

bool hv_launch_transport_begin(struct hv_launch_transport *transport,
                               const struct hv_contract_snapshot *snapshot)
{
    if (!transport || !snapshot || snapshot->header.magic != HV_CONTRACT_MAGIC ||
        snapshot->header.version != HV_CONTRACT_VERSION ||
        snapshot->header.header_size != sizeof(snapshot->header) ||
        snapshot->header.payload_size != sizeof(*snapshot) - sizeof(snapshot->header))
        return false;

    *transport = (struct hv_launch_transport){0};
    memcpy(transport->header.magic, HV_LAUNCH_FRAME_MAGIC, HV_LAUNCH_FRAME_MAGIC_SIZE);
    transport->header.version = HV_LAUNCH_FRAME_VERSION;
    transport->header.header_size = sizeof(transport->header);
    transport->header.payload_size = sizeof(*snapshot);
    transport->header.checkpoint = snapshot->header.checkpoint;
    transport->header.sequence = snapshot->header.sequence;
    transport->header.payload_crc32 = frame_crc32((const uint8_t *)snapshot, sizeof(*snapshot));
    transport->snapshot = snapshot;
    return true;
}

enum hv_launch_transport_result hv_launch_transport_pump(struct hv_launch_transport *transport,
                                                         hv_launch_transport_sink_fn sink,
                                                         void *opaque)
{
    const size_t header_size = sizeof(transport->header);
    const size_t total_size = header_size + sizeof(*transport->snapshot);
    const uint8_t *data;
    size_t remaining;
    size_t written;

    if (!transport || !transport->snapshot || !sink)
        return HV_LAUNCH_TRANSPORT_ERROR;
    if (transport->offset >= total_size)
        return HV_LAUNCH_TRANSPORT_COMPLETE;

    if (transport->offset < header_size) {
        data = (const uint8_t *)&transport->header + transport->offset;
        remaining = header_size - transport->offset;
    } else {
        size_t payload_offset = transport->offset - header_size;
        data = (const uint8_t *)transport->snapshot + payload_offset;
        remaining = sizeof(*transport->snapshot) - payload_offset;
    }

    written = sink(opaque, data, remaining);
    if (written > remaining)
        return HV_LAUNCH_TRANSPORT_ERROR;
    transport->offset += written;
    return transport->offset == total_size ? HV_LAUNCH_TRANSPORT_COMPLETE
                                           : HV_LAUNCH_TRANSPORT_PENDING;
}
