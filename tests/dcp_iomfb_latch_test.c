/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/dcp_iomfb_latch.h"

struct packet_header {
    char tag[4];
    uint32_t in_len;
    uint32_t out_len;
} __attribute__((packed));

static size_t input_size(enum dcp_iomfb_protocol_version version)
{
    return version == DCP_IOMFB_PROTOCOL_V13_5 ?
           DCP_IOMFB_V13_5_SWAP_COMPLETE_INPUT_SIZE :
           DCP_IOMFB_V12_3_SWAP_COMPLETE_INPUT_SIZE;
}

static size_t build_swap_complete(uint8_t *packet,
                                  enum dcp_iomfb_protocol_version version,
                                  uint32_t swap_id)
{
    struct packet_header *header = (void *)packet;
    size_t payload_size = input_size(version);

    memset(packet, 0, sizeof(*header) + payload_size);
    memcpy(header->tag, "985D", 4);
    header->in_len = payload_size;
    memcpy(packet + sizeof(*header), &swap_id, sizeof(swap_id));
    return sizeof(*header) + payload_size;
}

int main(void)
{
    uint8_t packet[sizeof(struct packet_header) +
                   DCP_IOMFB_V13_5_SWAP_COMPLETE_INPUT_SIZE];
    struct packet_header *header = (void *)packet;
    uint32_t completed = UINT32_MAX;
    size_t packet_size;

    packet_size = build_swap_complete(packet, DCP_IOMFB_PROTOCOL_V13_5, 42);
    assert(dcp_iomfb_parse_swap_complete(DCP_IOMFB_PROTOCOL_V13_5, packet,
                                          packet_size, 42, &completed) ==
           DCP_IOMFB_LATCH_MATCHED);
    assert(completed == 42);
    assert(dcp_iomfb_parse_swap_complete_payload(
               DCP_IOMFB_PROTOCOL_V13_5, packet + sizeof(*header),
               DCP_IOMFB_V13_5_SWAP_COMPLETE_INPUT_SIZE, 42, &completed) ==
           DCP_IOMFB_LATCH_MATCHED);
    assert(completed == 42);

    completed = UINT32_MAX;
    assert(dcp_iomfb_parse_swap_complete(DCP_IOMFB_PROTOCOL_V13_5, packet,
                                          packet_size, 41, &completed) ==
           DCP_IOMFB_LATCH_STALE);
    assert(completed == 42);

    /* Exact ABI versioning is mandatory: v13.5 packets are not v12.3 packets. */
    assert(dcp_iomfb_parse_swap_complete(DCP_IOMFB_PROTOCOL_V12_3, packet,
                                          packet_size, 42, &completed) ==
           DCP_IOMFB_LATCH_INVALID);

    memcpy(header->tag, "885D", 4);
    assert(dcp_iomfb_parse_swap_complete(DCP_IOMFB_PROTOCOL_V13_5, packet,
                                          packet_size, 42, &completed) ==
           DCP_IOMFB_LATCH_UNRELATED);

    packet_size = build_swap_complete(packet, DCP_IOMFB_PROTOCOL_V13_5, 42);
    assert(dcp_iomfb_parse_swap_complete(DCP_IOMFB_PROTOCOL_V13_5, packet,
                                          packet_size - 1, 42, &completed) ==
           DCP_IOMFB_LATCH_INVALID);
    header->in_len--;
    assert(dcp_iomfb_parse_swap_complete(DCP_IOMFB_PROTOCOL_V13_5, packet,
                                          packet_size, 42, &completed) ==
           DCP_IOMFB_LATCH_INVALID);
    header->in_len = DCP_IOMFB_V13_5_SWAP_COMPLETE_INPUT_SIZE;
    header->out_len = 4;
    assert(dcp_iomfb_parse_swap_complete(DCP_IOMFB_PROTOCOL_V13_5, packet,
                                          packet_size, 42, &completed) ==
           DCP_IOMFB_LATCH_INVALID);

    packet_size = build_swap_complete(packet, DCP_IOMFB_PROTOCOL_V12_3, 0);
    assert(dcp_iomfb_parse_swap_complete(DCP_IOMFB_PROTOCOL_V12_3, packet,
                                          packet_size, 42, &completed) ==
           DCP_IOMFB_LATCH_INVALID);
    assert(dcp_iomfb_parse_swap_complete(DCP_IOMFB_PROTOCOL_V13_5, NULL, 0,
                                          42, &completed) ==
           DCP_IOMFB_LATCH_INVALID);
    packet_size = build_swap_complete(packet, DCP_IOMFB_PROTOCOL_V12_3, 42);
    assert(dcp_iomfb_parse_swap_complete(DCP_IOMFB_PROTOCOL_V12_3, packet,
                                          packet_size, 0, &completed) ==
           DCP_IOMFB_LATCH_STALE);

    puts("dcp_iomfb_latch_test: ok");
    return 0;
}
