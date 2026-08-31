/* SPDX-License-Identifier: MIT */

#include "dcp_iomfb_transport.h"

#ifdef DCP_IOMFB_TRANSPORT_HOST_TEST
#include <string.h>
#else
#include "string.h"
#endif

#define DCP_IOMFB_MESSAGE_TYPE_MASK 0xfull
#define DCP_IOMFB_MSG_CONTEXT_MASK  0xfull
#define DCP_IOMFB_MSG_OFFSET_MASK   0xffffull
#define DCP_IOMFB_MSG_ACK           (1ull << 6)

static bool dcp_iomfb_context_offset(unsigned int context, size_t *offset)
{
    switch (context) {
        case DCP_IOMFB_CONTEXT_CB:
            *offset = 0x60000;
            return true;
        case DCP_IOMFB_CONTEXT_CMD:
            *offset = 0x00000;
            return true;
        case DCP_IOMFB_CONTEXT_ASYNC:
            *offset = 0x40000;
            return true;
        case DCP_IOMFB_CONTEXT_OOBCB:
            *offset = 0x68000;
            return true;
        case DCP_IOMFB_CONTEXT_OOBCMD:
            *offset = 0x08000;
            return true;
        case DCP_IOMFB_CONTEXT_OOBASYNC:
            *offset = 0x48000;
            return true;
        default:
            return false;
    }
}

dcp_iomfb_u64 dcp_iomfb_set_shmem_message(dcp_iomfb_u64 dva)
{
    return (dva & ~0xffffull) | (4ull << 4) | DCP_IOMFB_MESSAGE_TYPE_SET_SHMEM;
}

dcp_iomfb_u64 dcp_iomfb_ack_message(enum dcp_iomfb_context context)
{
    return DCP_IOMFB_MESSAGE_TYPE_MSG | ((dcp_iomfb_u64)context << 8) |
           DCP_IOMFB_MSG_ACK;
}

void dcp_iomfb_transport_init(
    struct dcp_iomfb_transport *transport,
    enum dcp_iomfb_protocol_version protocol_version, void *shmem,
    size_t shmem_size, dcp_iomfb_u64 shmem_dva, dcp_iomfb_send_fn send,
    void *send_opaque)
{
    if (!transport)
        return;

    memset(transport, 0, sizeof(*transport));
    transport->protocol_version = protocol_version;
    transport->shmem = shmem;
    transport->shmem_size = shmem_size;
    transport->shmem_dva = shmem_dva;
    transport->send = send;
    transport->send_opaque = send_opaque;
}

bool dcp_iomfb_transport_send_shmem(struct dcp_iomfb_transport *transport)
{
    if (!transport || !transport->shmem ||
        transport->shmem_size != DCP_IOMFB_SHMEM_SIZE ||
        (transport->shmem_dva & 0xffffull) || !transport->send)
        return false;

    if (!transport->send(transport->send_opaque, DCP_IOMFB_ENDPOINT,
                         dcp_iomfb_set_shmem_message(transport->shmem_dva))) {
        transport->state = DCP_IOMFB_FAILED;
        return false;
    }

    transport->state = DCP_IOMFB_WAIT_INITIALIZED;
    return true;
}

enum dcp_iomfb_rx_result dcp_iomfb_transport_receive(
    struct dcp_iomfb_transport *transport, dcp_iomfb_u64 message)
{
    unsigned int type;
    unsigned int context;
    size_t channel_offset;
    size_t packet_offset;
    size_t packet_size;
    dcp_iomfb_u32 completed_swap = 0;
    enum dcp_iomfb_latch_result parsed;

    if (!transport)
        return DCP_IOMFB_RX_INVALID;

    type = (unsigned int)(message & DCP_IOMFB_MESSAGE_TYPE_MASK);
    if (type == DCP_IOMFB_MESSAGE_TYPE_INITIALIZED) {
        if (transport->state != DCP_IOMFB_WAIT_INITIALIZED)
            return DCP_IOMFB_RX_INVALID;
        transport->state = DCP_IOMFB_READY;
        return DCP_IOMFB_RX_INITIALIZED;
    }

    if (type != DCP_IOMFB_MESSAGE_TYPE_MSG || transport->state != DCP_IOMFB_READY)
        return DCP_IOMFB_RX_INVALID;

    context = (unsigned int)((message >> DCP_IOMFB_MSG_CONTEXT_SHIFT) &
                             DCP_IOMFB_MSG_CONTEXT_MASK);
    if (context != DCP_IOMFB_CONTEXT_CB && context != DCP_IOMFB_CONTEXT_OOBCB)
        return DCP_IOMFB_RX_INVALID;
    if (message & DCP_IOMFB_MSG_ACK)
        return DCP_IOMFB_RX_UNHANDLED;
    if (!dcp_iomfb_context_offset(context, &channel_offset))
        return DCP_IOMFB_RX_INVALID;

    packet_offset = (size_t)((message >> DCP_IOMFB_MSG_OFFSET_SHIFT) &
                             DCP_IOMFB_MSG_OFFSET_MASK);
    packet_size = (size_t)(message >> DCP_IOMFB_MSG_LENGTH_SHIFT);
    if (channel_offset > transport->shmem_size ||
        packet_offset > transport->shmem_size - channel_offset ||
        packet_size > transport->shmem_size - channel_offset - packet_offset)
        return DCP_IOMFB_RX_INVALID;

    parsed = dcp_iomfb_parse_swap_complete(
        transport->protocol_version,
        transport->shmem + channel_offset + packet_offset, packet_size,
        transport->armed ? transport->expected_swap_id : transport->latched_swap_id,
        &completed_swap);
    if (parsed == DCP_IOMFB_LATCH_UNRELATED)
        return DCP_IOMFB_RX_UNHANDLED;
    if (parsed == DCP_IOMFB_LATCH_INVALID)
        return DCP_IOMFB_RX_INVALID;

    if (!transport->send(transport->send_opaque, DCP_IOMFB_ENDPOINT,
                         dcp_iomfb_ack_message((enum dcp_iomfb_context)context))) {
        transport->state = DCP_IOMFB_FAILED;
        return DCP_IOMFB_RX_INVALID;
    }

    if (!transport->armed)
        return completed_swap == transport->latched_swap_id ? DCP_IOMFB_RX_DUPLICATE :
                                                              DCP_IOMFB_RX_STALE;
    if (parsed == DCP_IOMFB_LATCH_STALE || completed_swap != transport->expected_swap_id)
        return DCP_IOMFB_RX_STALE;

    transport->latched_swap_id = completed_swap;
    transport->armed = false;
    return DCP_IOMFB_RX_LATCHED;
}

void dcp_iomfb_transport_arm_swap(struct dcp_iomfb_transport *transport,
                                  dcp_iomfb_u32 swap_id)
{
    if (!transport || !swap_id || transport->state != DCP_IOMFB_READY)
        return;
    transport->expected_swap_id = swap_id;
    transport->armed = true;
}

enum dcp_iomfb_transport_state dcp_iomfb_transport_state(
    const struct dcp_iomfb_transport *transport)
{
    return transport ? transport->state : DCP_IOMFB_FAILED;
}

dcp_iomfb_u32 dcp_iomfb_transport_latched_swap(
    const struct dcp_iomfb_transport *transport)
{
    return transport ? transport->latched_swap_id : 0;
}
