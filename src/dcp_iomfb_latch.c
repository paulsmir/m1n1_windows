/* SPDX-License-Identifier: MIT */

#include "dcp_iomfb_latch.h"
#include "string.h"

struct dcp_iomfb_packet_header {
    char tag[4];
    uint32_t in_len;
    uint32_t out_len;
} __attribute__((packed));

static bool dcp_iomfb_is_swap_complete_tag(const char tag[4])
{
    return tag[0] == '9' && tag[1] == '8' && tag[2] == '5' && tag[3] == 'D';
}

static size_t dcp_iomfb_swap_complete_input_size(
    enum dcp_iomfb_protocol_version version)
{
    switch (version) {
        case DCP_IOMFB_PROTOCOL_V12_3:
            return DCP_IOMFB_V12_3_SWAP_COMPLETE_INPUT_SIZE;
        case DCP_IOMFB_PROTOCOL_V13_5:
            return DCP_IOMFB_V13_5_SWAP_COMPLETE_INPUT_SIZE;
        default:
            return 0;
    }
}

enum dcp_iomfb_latch_result dcp_iomfb_parse_swap_complete(
    enum dcp_iomfb_protocol_version version, const void *packet,
    size_t packet_size, uint32_t expected_swap_id,
    uint32_t *completed_swap_id)
{
    const struct dcp_iomfb_packet_header *header = packet;
    size_t expected_input_size;
    size_t total_size;

    if (completed_swap_id)
        *completed_swap_id = 0;
    if (!packet || !completed_swap_id || packet_size < sizeof(*header))
        return DCP_IOMFB_LATCH_INVALID;
    if (!dcp_iomfb_is_swap_complete_tag(header->tag))
        return DCP_IOMFB_LATCH_UNRELATED;
    expected_input_size = dcp_iomfb_swap_complete_input_size(version);
    if (!expected_input_size || header->in_len != expected_input_size ||
        header->out_len != 0)
        return DCP_IOMFB_LATCH_INVALID;
    total_size = sizeof(*header) + (size_t)header->in_len;
    if (total_size < sizeof(*header) || packet_size != total_size ||
        header->in_len < sizeof(uint32_t))
        return DCP_IOMFB_LATCH_INVALID;

    return dcp_iomfb_parse_swap_complete_payload(
        version, (const uint8_t *)packet + sizeof(*header), header->in_len,
        expected_swap_id, completed_swap_id);
}

enum dcp_iomfb_latch_result dcp_iomfb_parse_swap_complete_payload(
    enum dcp_iomfb_protocol_version version, const void *payload,
    size_t payload_size, uint32_t expected_swap_id,
    uint32_t *completed_swap_id)
{
    uint32_t swap_id;
    size_t expected_input_size;

    if (completed_swap_id)
        *completed_swap_id = 0;
    expected_input_size = dcp_iomfb_swap_complete_input_size(version);
    if (!payload || !completed_swap_id || !expected_input_size ||
        payload_size != expected_input_size || payload_size < sizeof(swap_id))
        return DCP_IOMFB_LATCH_INVALID;
    memcpy(&swap_id, payload, sizeof(swap_id));
    if (swap_id == 0)
        return DCP_IOMFB_LATCH_INVALID;
    *completed_swap_id = swap_id;
    if (!expected_swap_id || swap_id != expected_swap_id)
        return DCP_IOMFB_LATCH_STALE;
    return DCP_IOMFB_LATCH_MATCHED;
}
