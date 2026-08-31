/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/dcp_iomfb_transport.h"

struct send_capture {
    unsigned int calls;
    uint8_t endpoint;
    uint64_t message;
};

struct packet_header {
    char tag[4];
    uint32_t in_len;
    uint32_t out_len;
} __attribute__((packed));

static bool capture_send(void *opaque, uint8_t endpoint, uint64_t message)
{
    struct send_capture *capture = opaque;
    capture->calls++;
    capture->endpoint = endpoint;
    capture->message = message;
    return true;
}

static size_t build_swap_complete(uint8_t *shmem, size_t offset, uint32_t swap_id)
{
    struct packet_header *header = (void *)(shmem + offset);
    size_t size = sizeof(*header) + DCP_IOMFB_V13_5_SWAP_COMPLETE_INPUT_SIZE;

    memset(header, 0, size);
    memcpy(header->tag, "985D", 4);
    header->in_len = DCP_IOMFB_V13_5_SWAP_COMPLETE_INPUT_SIZE;
    memcpy(header + 1, &swap_id, sizeof(swap_id));
    return size;
}

static uint64_t message(unsigned int context, size_t offset, size_t length)
{
    return DCP_IOMFB_MESSAGE_TYPE_MSG |
           ((uint64_t)context << DCP_IOMFB_MSG_CONTEXT_SHIFT) |
           ((uint64_t)offset << DCP_IOMFB_MSG_OFFSET_SHIFT) |
           ((uint64_t)length << DCP_IOMFB_MSG_LENGTH_SHIFT);
}

int main(void)
{
    uint8_t shmem[DCP_IOMFB_SHMEM_SIZE];
    struct send_capture sent = {0};
    struct dcp_iomfb_transport transport;
    const size_t packet_offset = 0x60000;
    size_t packet_size;

    memset(shmem, 0, sizeof(shmem));
    dcp_iomfb_transport_init(&transport, DCP_IOMFB_PROTOCOL_V13_5, shmem,
                             sizeof(shmem), 0x12340000, capture_send, &sent);

    assert(dcp_iomfb_transport_send_shmem(&transport));
    assert(sent.calls == 1);
    assert(sent.endpoint == DCP_IOMFB_ENDPOINT);
    assert(sent.message == dcp_iomfb_set_shmem_message(0x12340000));
    assert(dcp_iomfb_transport_state(&transport) == DCP_IOMFB_WAIT_INITIALIZED);

    assert(dcp_iomfb_transport_receive(&transport, DCP_IOMFB_MESSAGE_TYPE_INITIALIZED) ==
           DCP_IOMFB_RX_INITIALIZED);
    assert(dcp_iomfb_transport_state(&transport) == DCP_IOMFB_READY);

    dcp_iomfb_transport_arm_swap(&transport, 42);
    packet_size = build_swap_complete(shmem, packet_offset, 42);
    assert(dcp_iomfb_transport_receive(&transport,
               message(DCP_IOMFB_CONTEXT_CB, 0, packet_size)) == DCP_IOMFB_RX_LATCHED);
    assert(dcp_iomfb_transport_latched_swap(&transport) == 42);
    assert(sent.calls == 2);
    assert(sent.message == dcp_iomfb_ack_message(DCP_IOMFB_CONTEXT_CB));

    /* Duplicate callback is acknowledged, but cannot produce another latch. */
    assert(dcp_iomfb_transport_receive(&transport,
               message(DCP_IOMFB_CONTEXT_CB, 0, packet_size)) == DCP_IOMFB_RX_DUPLICATE);
    assert(sent.calls == 3);

    dcp_iomfb_transport_arm_swap(&transport, 43);
    assert(dcp_iomfb_transport_receive(&transport,
               message(DCP_IOMFB_CONTEXT_CB, 0, packet_size)) == DCP_IOMFB_RX_STALE);
    assert(dcp_iomfb_transport_latched_swap(&transport) == 42);
    assert(sent.calls == 4);

    /* Unknown callbacks remain unacked for the bootstrap dispatcher. */
    memcpy(shmem + packet_offset, "000D", 4);
    assert(dcp_iomfb_transport_receive(&transport,
               message(DCP_IOMFB_CONTEXT_CB, 0, packet_size)) == DCP_IOMFB_RX_UNHANDLED);
    assert(sent.calls == 4);

    assert(dcp_iomfb_transport_receive(&transport,
               message(1, 0, packet_size)) == DCP_IOMFB_RX_INVALID);
    assert(dcp_iomfb_transport_receive(&transport,
               message(DCP_IOMFB_CONTEXT_CB, 0xffff, 0xffffffffu)) == DCP_IOMFB_RX_INVALID);
    assert(dcp_iomfb_transport_receive(&transport, 0xf) == DCP_IOMFB_RX_INVALID);

    puts("dcp_iomfb_transport_test: ok");
    return 0;
}
