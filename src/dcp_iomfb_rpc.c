/* SPDX-License-Identifier: MIT */

#include "dcp_iomfb_rpc.h"

#ifdef DCP_IOMFB_RPC_HOST_TEST
#include <string.h>
#else
#include "string.h"
#endif

#define DCP_IOMFB_RPC_HEADER_SIZE       12u
#define DCP_IOMFB_RPC_PACKET_ALIGNMENT  0x40u
#define DCP_IOMFB_RPC_TYPE_MSG          2ull
#define DCP_IOMFB_RPC_ACK               (1ull << 6)
#define DCP_IOMFB_RPC_CONTEXT_SHIFT     8u
#define DCP_IOMFB_RPC_OFFSET_SHIFT      16u
#define DCP_IOMFB_RPC_LENGTH_SHIFT      32u
#define DCP_IOMFB_RPC_DEFAULT_MAX_PUMPS 100000u

static size_t dcp_iomfb_rpc_align_packet(size_t size)
{
    return (size + DCP_IOMFB_RPC_PACKET_ALIGNMENT - 1) &
           ~(DCP_IOMFB_RPC_PACKET_ALIGNMENT - 1);
}

static dcp_iomfb_rpc_u32 dcp_iomfb_rpc_load_u32(const void *pointer)
{
    dcp_iomfb_rpc_u32 value;

    memcpy(&value, pointer, sizeof(value));
    return value;
}

static void dcp_iomfb_rpc_store_u32(void *pointer, dcp_iomfb_rpc_u32 value)
{
    memcpy(pointer, &value, sizeof(value));
}

dcp_iomfb_rpc_u64 dcp_iomfb_rpc_message(
    enum dcp_iomfb_rpc_context context, size_t offset, size_t length,
    bool ack)
{
    if (offset > 0xffffu || length > 0xffffffffu)
        return 0;
    return DCP_IOMFB_RPC_TYPE_MSG |
           ((dcp_iomfb_rpc_u64)context << DCP_IOMFB_RPC_CONTEXT_SHIFT) |
           ((dcp_iomfb_rpc_u64)offset << DCP_IOMFB_RPC_OFFSET_SHIFT) |
           ((dcp_iomfb_rpc_u64)length << DCP_IOMFB_RPC_LENGTH_SHIFT) |
           (ack ? DCP_IOMFB_RPC_ACK : 0);
}

void dcp_iomfb_rpc_init(struct dcp_iomfb_rpc *rpc, void *shmem,
                        size_t shmem_size, dcp_iomfb_rpc_send_fn send,
                        dcp_iomfb_rpc_pump_fn pump,
                        dcp_iomfb_rpc_callback_fn callback, void *opaque)
{
    if (!rpc)
        return;
    memset(rpc, 0, sizeof(*rpc));
    rpc->shmem = shmem;
    rpc->shmem_size = shmem_size;
    rpc->send = send;
    rpc->pump = pump;
    rpc->callback = callback;
    rpc->opaque = opaque;
    rpc->max_pumps = DCP_IOMFB_RPC_DEFAULT_MAX_PUMPS;
}

bool dcp_iomfb_rpc_call(struct dcp_iomfb_rpc *rpc, const char tag[4],
                        const void *input, dcp_iomfb_rpc_u32 input_size,
                        void *output, dcp_iomfb_rpc_u32 output_size)
{
    struct dcp_iomfb_rpc_frame *frame;
    enum dcp_iomfb_rpc_context context;
    dcp_iomfb_rpc_u8 *packet;
    size_t packet_size;
    size_t offset;
    size_t end;
    unsigned int depth;
    unsigned int pumps = 0;

    if (!rpc || !rpc->shmem || rpc->shmem_size != DCP_IOMFB_RPC_SHMEM_SIZE ||
        !rpc->send || !rpc->pump || !tag || (input_size && !input) ||
        (output_size && !output) || rpc->tx_depth >= DCP_IOMFB_RPC_MAX_DEPTH)
        return false;
    packet_size = DCP_IOMFB_RPC_HEADER_SIZE + (size_t)input_size + output_size;
    if (packet_size < input_size || packet_size < output_size)
        return false;

    depth = rpc->tx_depth;
    offset = depth ? rpc->tx[depth - 1].end : 0;
    end = offset + dcp_iomfb_rpc_align_packet(packet_size);
    if (end < offset || end > DCP_IOMFB_RPC_CHANNEL_SIZE ||
        end > rpc->shmem_size)
        return false;

    packet = rpc->shmem + offset;
    memset(packet, 0, packet_size);
    packet[0] = (dcp_iomfb_rpc_u8)tag[3];
    packet[1] = (dcp_iomfb_rpc_u8)tag[2];
    packet[2] = (dcp_iomfb_rpc_u8)tag[1];
    packet[3] = (dcp_iomfb_rpc_u8)tag[0];
    dcp_iomfb_rpc_store_u32(packet + 4, input_size);
    dcp_iomfb_rpc_store_u32(packet + 8, output_size);
    if (input_size)
        memcpy(packet + DCP_IOMFB_RPC_HEADER_SIZE, input, input_size);

    frame = &rpc->tx[depth];
    frame->offset = offset;
    frame->end = end;
    frame->context = context =
        depth ? DCP_IOMFB_RPC_CONTEXT_CB : DCP_IOMFB_RPC_CONTEXT_CMD;
    frame->complete = false;
    rpc->tx_depth++;
    if (!rpc->send(rpc->opaque, DCP_IOMFB_RPC_ENDPOINT,
                   dcp_iomfb_rpc_message(context, offset, packet_size, false)))
        goto fail;

    while (!frame->complete) {
        if (++pumps > rpc->max_pumps || rpc->pump(rpc->opaque) < 0)
            goto fail;
    }

    if (output_size)
        memcpy(output, packet + DCP_IOMFB_RPC_HEADER_SIZE + input_size,
               output_size);
    rpc->tx_depth--;
    memset(frame, 0, sizeof(*frame));
    return true;

fail:
    rpc->tx_depth--;
    memset(frame, 0, sizeof(*frame));
    return false;
}

static bool dcp_iomfb_rpc_callback_base(unsigned int context, size_t *base)
{
    switch (context) {
        case DCP_IOMFB_RPC_CONTEXT_ASYNC:
            *base = DCP_IOMFB_RPC_ASYNC_OFFSET;
            return true;
        case DCP_IOMFB_RPC_CONTEXT_OOBASYNC:
            *base = DCP_IOMFB_RPC_OOBASYNC_OFFSET;
            return true;
        case DCP_IOMFB_RPC_CONTEXT_CB:
            *base = DCP_IOMFB_RPC_CB_OFFSET;
            return true;
        case DCP_IOMFB_RPC_CONTEXT_OOBCB:
            *base = DCP_IOMFB_RPC_OOBCB_OFFSET;
            return true;
        default:
            return false;
    }
}

enum dcp_iomfb_rpc_rx_result dcp_iomfb_rpc_receive(
    struct dcp_iomfb_rpc *rpc, dcp_iomfb_rpc_u64 message)
{
    dcp_iomfb_rpc_u8 *packet;
    char tag[4];
    unsigned int context;
    size_t base;
    size_t offset;
    size_t length;
    size_t data_size;
    dcp_iomfb_rpc_u32 input_size;
    dcp_iomfb_rpc_u32 output_size;

    if (!rpc || !rpc->shmem || (message & 0xf) != DCP_IOMFB_RPC_TYPE_MSG)
        return DCP_IOMFB_RPC_RX_INVALID;
    context = (unsigned int)((message >> DCP_IOMFB_RPC_CONTEXT_SHIFT) & 0xf);
    offset = (size_t)((message >> DCP_IOMFB_RPC_OFFSET_SHIFT) & 0xffff);
    length = (size_t)(message >> DCP_IOMFB_RPC_LENGTH_SHIFT);
    if (message & DCP_IOMFB_RPC_ACK) {
        if (!rpc->tx_depth ||
            context != rpc->tx[rpc->tx_depth - 1].context || offset != 0 ||
            length != 0)
            return DCP_IOMFB_RPC_RX_INVALID;
        rpc->tx[rpc->tx_depth - 1].complete = true;
        return DCP_IOMFB_RPC_RX_ACK;
    }
    if (!rpc->callback || !dcp_iomfb_rpc_callback_base(context, &base))
        return DCP_IOMFB_RPC_RX_UNHANDLED;

    if (length < DCP_IOMFB_RPC_HEADER_SIZE ||
        offset > DCP_IOMFB_RPC_CHANNEL_SIZE ||
        length > DCP_IOMFB_RPC_CHANNEL_SIZE - offset ||
        base > rpc->shmem_size ||
        DCP_IOMFB_RPC_CHANNEL_SIZE > rpc->shmem_size - base)
        return DCP_IOMFB_RPC_RX_INVALID;
    packet = rpc->shmem + base + offset;
    input_size = dcp_iomfb_rpc_load_u32(packet + 4);
    output_size = dcp_iomfb_rpc_load_u32(packet + 8);
    data_size = DCP_IOMFB_RPC_HEADER_SIZE + (size_t)input_size + output_size;
    if (data_size < input_size || data_size < output_size || data_size != length)
        return DCP_IOMFB_RPC_RX_INVALID;

    tag[0] = (char)packet[3];
    tag[1] = (char)packet[2];
    tag[2] = (char)packet[1];
    tag[3] = (char)packet[0];
    if (rpc->callback(rpc->opaque, tag,
                      packet + DCP_IOMFB_RPC_HEADER_SIZE, input_size,
                      packet + DCP_IOMFB_RPC_HEADER_SIZE + input_size,
                      output_size) != 0)
        return DCP_IOMFB_RPC_RX_INVALID;
    if (!rpc->send(rpc->opaque, DCP_IOMFB_RPC_ENDPOINT,
                   dcp_iomfb_rpc_message(
                       (enum dcp_iomfb_rpc_context)context, 0, 0, true)))
        return DCP_IOMFB_RPC_RX_INVALID;
    return DCP_IOMFB_RPC_RX_CALLBACK;
}

bool dcp_iomfb_rpc_idle(const struct dcp_iomfb_rpc *rpc)
{
    return rpc && rpc->tx_depth == 0;
}
