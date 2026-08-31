/* SPDX-License-Identifier: MIT */

#ifndef DCP_IOMFB_TRANSPORT_H
#define DCP_IOMFB_TRANSPORT_H

#ifdef DCP_IOMFB_TRANSPORT_HOST_TEST
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t dcp_iomfb_u8;
typedef uint32_t dcp_iomfb_u32;
typedef uint64_t dcp_iomfb_u64;
#else
#include "types.h"
typedef u8 dcp_iomfb_u8;
typedef u32 dcp_iomfb_u32;
typedef u64 dcp_iomfb_u64;
#endif

#include "dcp_iomfb_latch.h"

#define DCP_IOMFB_ENDPOINT             0x37u
#define DCP_IOMFB_SHMEM_SIZE           0x100000u
#define DCP_IOMFB_MESSAGE_TYPE_SET_SHMEM 0u
#define DCP_IOMFB_MESSAGE_TYPE_INITIALIZED 1u
#define DCP_IOMFB_MESSAGE_TYPE_MSG     2u
#define DCP_IOMFB_MSG_CONTEXT_SHIFT    8u
#define DCP_IOMFB_MSG_OFFSET_SHIFT     16u
#define DCP_IOMFB_MSG_LENGTH_SHIFT     32u

enum dcp_iomfb_context {
    DCP_IOMFB_CONTEXT_CB = 0,
    DCP_IOMFB_CONTEXT_CMD = 2,
    DCP_IOMFB_CONTEXT_ASYNC = 3,
    DCP_IOMFB_CONTEXT_OOBCB = 4,
    DCP_IOMFB_CONTEXT_OOBCMD = 6,
    DCP_IOMFB_CONTEXT_OOBASYNC = 7,
};

enum dcp_iomfb_transport_state {
    DCP_IOMFB_OFF = 0,
    DCP_IOMFB_WAIT_INITIALIZED,
    DCP_IOMFB_READY,
    DCP_IOMFB_FAILED,
};

enum dcp_iomfb_rx_result {
    DCP_IOMFB_RX_INVALID = -1,
    DCP_IOMFB_RX_UNHANDLED = 0,
    DCP_IOMFB_RX_INITIALIZED = 1,
    DCP_IOMFB_RX_LATCHED = 2,
    DCP_IOMFB_RX_STALE = 3,
    DCP_IOMFB_RX_DUPLICATE = 4,
};

typedef bool (*dcp_iomfb_send_fn)(void *opaque, dcp_iomfb_u8 endpoint,
                                  dcp_iomfb_u64 message);

struct dcp_iomfb_transport {
    enum dcp_iomfb_protocol_version protocol_version;
    dcp_iomfb_u8 *shmem;
    size_t shmem_size;
    dcp_iomfb_u64 shmem_dva;
    dcp_iomfb_send_fn send;
    void *send_opaque;
    enum dcp_iomfb_transport_state state;
    dcp_iomfb_u32 expected_swap_id;
    dcp_iomfb_u32 latched_swap_id;
    bool armed;
};

void dcp_iomfb_transport_init(
    struct dcp_iomfb_transport *transport,
    enum dcp_iomfb_protocol_version protocol_version, void *shmem,
    size_t shmem_size, dcp_iomfb_u64 shmem_dva, dcp_iomfb_send_fn send,
    void *send_opaque);
bool dcp_iomfb_transport_send_shmem(struct dcp_iomfb_transport *transport);
enum dcp_iomfb_rx_result dcp_iomfb_transport_receive(
    struct dcp_iomfb_transport *transport, dcp_iomfb_u64 message);
void dcp_iomfb_transport_arm_swap(struct dcp_iomfb_transport *transport,
                                  dcp_iomfb_u32 swap_id);
enum dcp_iomfb_transport_state dcp_iomfb_transport_state(
    const struct dcp_iomfb_transport *transport);
dcp_iomfb_u32 dcp_iomfb_transport_latched_swap(
    const struct dcp_iomfb_transport *transport);
dcp_iomfb_u64 dcp_iomfb_set_shmem_message(dcp_iomfb_u64 dva);
dcp_iomfb_u64 dcp_iomfb_ack_message(enum dcp_iomfb_context context);

#endif
