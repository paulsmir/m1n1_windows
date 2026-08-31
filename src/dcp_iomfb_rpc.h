/* SPDX-License-Identifier: MIT */

#ifndef DCP_IOMFB_RPC_H
#define DCP_IOMFB_RPC_H

#ifdef DCP_IOMFB_RPC_HOST_TEST
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t dcp_iomfb_rpc_u8;
typedef uint32_t dcp_iomfb_rpc_u32;
typedef uint64_t dcp_iomfb_rpc_u64;
#else
#include "types.h"
typedef u8 dcp_iomfb_rpc_u8;
typedef u32 dcp_iomfb_rpc_u32;
typedef u64 dcp_iomfb_rpc_u64;
#endif

#define DCP_IOMFB_RPC_ENDPOINT       0x37u
#define DCP_IOMFB_RPC_SHMEM_SIZE     0x100000u
#define DCP_IOMFB_RPC_CHANNEL_SIZE   0x8000u
#define DCP_IOMFB_RPC_ASYNC_OFFSET   0x40000u
#define DCP_IOMFB_RPC_OOBASYNC_OFFSET 0x48000u
#define DCP_IOMFB_RPC_CB_OFFSET      0x60000u
#define DCP_IOMFB_RPC_OOBCB_OFFSET   0x68000u
#define DCP_IOMFB_RPC_MAX_DEPTH      8u

enum dcp_iomfb_rpc_context {
    DCP_IOMFB_RPC_CONTEXT_CB = 0,
    DCP_IOMFB_RPC_CONTEXT_CMD = 2,
    DCP_IOMFB_RPC_CONTEXT_ASYNC = 3,
    DCP_IOMFB_RPC_CONTEXT_OOBCB = 4,
    DCP_IOMFB_RPC_CONTEXT_OOBCMD = 6,
    DCP_IOMFB_RPC_CONTEXT_OOBASYNC = 7,
};

enum dcp_iomfb_rpc_rx_result {
    DCP_IOMFB_RPC_RX_INVALID = -1,
    DCP_IOMFB_RPC_RX_UNHANDLED = 0,
    DCP_IOMFB_RPC_RX_ACK = 1,
    DCP_IOMFB_RPC_RX_CALLBACK = 2,
};

typedef bool (*dcp_iomfb_rpc_send_fn)(void *opaque, dcp_iomfb_rpc_u8 endpoint,
                                      dcp_iomfb_rpc_u64 message);
typedef int (*dcp_iomfb_rpc_pump_fn)(void *opaque);
typedef int (*dcp_iomfb_rpc_callback_fn)(
    void *opaque, const char tag[4], const void *input,
    dcp_iomfb_rpc_u32 input_size, void *output,
    dcp_iomfb_rpc_u32 output_size);

struct dcp_iomfb_rpc_frame {
    size_t offset;
    size_t end;
    enum dcp_iomfb_rpc_context context;
    bool complete;
};

struct dcp_iomfb_rpc {
    dcp_iomfb_rpc_u8 *shmem;
    size_t shmem_size;
    dcp_iomfb_rpc_send_fn send;
    dcp_iomfb_rpc_pump_fn pump;
    dcp_iomfb_rpc_callback_fn callback;
    void *opaque;
    struct dcp_iomfb_rpc_frame tx[DCP_IOMFB_RPC_MAX_DEPTH];
    unsigned int tx_depth;
    unsigned int max_pumps;
};

void dcp_iomfb_rpc_init(struct dcp_iomfb_rpc *rpc, void *shmem,
                        size_t shmem_size, dcp_iomfb_rpc_send_fn send,
                        dcp_iomfb_rpc_pump_fn pump,
                        dcp_iomfb_rpc_callback_fn callback, void *opaque);
bool dcp_iomfb_rpc_call(struct dcp_iomfb_rpc *rpc, const char tag[4],
                        const void *input, dcp_iomfb_rpc_u32 input_size,
                        void *output, dcp_iomfb_rpc_u32 output_size);
enum dcp_iomfb_rpc_rx_result dcp_iomfb_rpc_receive(
    struct dcp_iomfb_rpc *rpc, dcp_iomfb_rpc_u64 message);
bool dcp_iomfb_rpc_idle(const struct dcp_iomfb_rpc *rpc);
dcp_iomfb_rpc_u64 dcp_iomfb_rpc_message(
    enum dcp_iomfb_rpc_context context, size_t offset, size_t length,
    bool ack);

#endif
