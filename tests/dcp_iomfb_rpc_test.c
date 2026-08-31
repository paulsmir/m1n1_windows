/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/dcp_iomfb_rpc.h"

struct fixture {
    struct dcp_iomfb_rpc rpc;
    uint8_t shmem[DCP_IOMFB_RPC_SHMEM_SIZE];
    unsigned sends;
    uint64_t last_message;
    unsigned pump_step;
    unsigned callbacks;
    unsigned nested_calls;
};

static bool capture_send(void *opaque, uint8_t endpoint, uint64_t message)
{
    struct fixture *fixture = opaque;

    assert(endpoint == DCP_IOMFB_RPC_ENDPOINT);
    fixture->sends++;
    fixture->last_message = message;
    return true;
}

static size_t put_callback(struct fixture *fixture, const char tag[4],
                           uint32_t in_len, uint32_t out_len)
{
    uint8_t *packet = fixture->shmem + DCP_IOMFB_RPC_CB_OFFSET;

    memset(packet, 0, 12 + in_len + out_len);
    memcpy(packet, tag, 4);
    memcpy(packet + 4, &in_len, 4);
    memcpy(packet + 8, &out_len, 4);
    return 12 + in_len + out_len;
}

static int handle_callback(void *opaque, const char tag[4], const void *input,
                           uint32_t in_len, void *output, uint32_t out_len)
{
    struct fixture *fixture = opaque;
    uint32_t nested_result = 0;
    uint8_t success = 1;

    (void)input;
    fixture->callbacks++;
    if (memcmp(tag, "D589", 4) == 0) {
        assert(in_len == 0x6f0);
        assert(out_len == 0);
        return 0;
    }
    assert(memcmp(tag, "D120", 4) == 0);
    assert(in_len == 0);
    assert(out_len == 4);

    assert(dcp_iomfb_rpc_call(&fixture->rpc, "A373", NULL, 0,
                              &nested_result, 4));
    assert(nested_result == 0x373);
    fixture->nested_calls++;
    memcpy(output, &success, sizeof(success));
    return 0;
}

static int pump(void *opaque)
{
    struct fixture *fixture = opaque;
    uint32_t value;
    size_t length;

    switch (fixture->pump_step++) {
        case 0:
            length = put_callback(fixture, "021D", 0, 4);
            assert(dcp_iomfb_rpc_receive(
                       &fixture->rpc,
                       dcp_iomfb_rpc_message(DCP_IOMFB_RPC_CONTEXT_CB, 0,
                                             length, false)) ==
                   DCP_IOMFB_RPC_RX_CALLBACK);
            return 0;
        case 1:
            value = 0x373;
            memcpy(fixture->shmem + 0x40 + 12, &value, sizeof(value));
            assert(dcp_iomfb_rpc_receive(
                       &fixture->rpc,
                       dcp_iomfb_rpc_message(DCP_IOMFB_RPC_CONTEXT_CB, 0, 0,
                                             true)) ==
                   DCP_IOMFB_RPC_RX_ACK);
            return 0;
        case 2:
            value = 0x401;
            memcpy(fixture->shmem + 12, &value, sizeof(value));
            assert(dcp_iomfb_rpc_receive(
                       &fixture->rpc,
                       dcp_iomfb_rpc_message(DCP_IOMFB_RPC_CONTEXT_CMD, 0, 1,
                                             true)) ==
                   DCP_IOMFB_RPC_RX_INVALID);
            assert(dcp_iomfb_rpc_receive(
                       &fixture->rpc,
                       dcp_iomfb_rpc_message(DCP_IOMFB_RPC_CONTEXT_CMD, 0, 0,
                                             true)) ==
                   DCP_IOMFB_RPC_RX_ACK);
            return 0;
        default:
            return -1;
    }
}

int main(void)
{
    struct fixture fixture;
    uint32_t result = 0;
    const uint8_t *packet;
    uint32_t in_len;
    uint32_t out_len;

    memset(&fixture, 0, sizeof(fixture));
    dcp_iomfb_rpc_init(&fixture.rpc, fixture.shmem, sizeof(fixture.shmem),
                       capture_send, pump, handle_callback, &fixture);

    assert(dcp_iomfb_rpc_receive(
               &fixture.rpc,
               dcp_iomfb_rpc_message(DCP_IOMFB_RPC_CONTEXT_OOBCMD, 0, 0,
                                     true)) ==
           DCP_IOMFB_RPC_RX_INVALID);

    assert(dcp_iomfb_rpc_call(&fixture.rpc, "A401", NULL, 0, &result, 4));
    assert(result == 0x401);
    assert(fixture.callbacks == 1);
    assert(fixture.nested_calls == 1);
    assert(fixture.sends == 3); /* A401, A373, callback ACK, no extra traffic. */
    assert(dcp_iomfb_rpc_idle(&fixture.rpc));

    memset(fixture.shmem + DCP_IOMFB_RPC_ASYNC_OFFSET, 0, 12 + 0x6f0);
    memcpy(fixture.shmem + DCP_IOMFB_RPC_ASYNC_OFFSET, "985D", 4);
    in_len = 0x6f0;
    out_len = 0;
    memcpy(fixture.shmem + DCP_IOMFB_RPC_ASYNC_OFFSET + 4, &in_len, 4);
    memcpy(fixture.shmem + DCP_IOMFB_RPC_ASYNC_OFFSET + 8, &out_len, 4);
    assert(dcp_iomfb_rpc_receive(
               &fixture.rpc,
               dcp_iomfb_rpc_message(DCP_IOMFB_RPC_CONTEXT_ASYNC, 0,
                                     12 + 0x6f0, false)) ==
           DCP_IOMFB_RPC_RX_CALLBACK);
    assert(fixture.callbacks == 2);
    assert(fixture.sends == 4);

    assert(dcp_iomfb_rpc_receive(
               &fixture.rpc,
               dcp_iomfb_rpc_message(DCP_IOMFB_RPC_CONTEXT_ASYNC,
                                     DCP_IOMFB_RPC_CHANNEL_SIZE - 8, 16,
                                     false)) ==
           DCP_IOMFB_RPC_RX_INVALID);

    packet = fixture.shmem;
    assert(memcmp(packet, "104A", 4) == 0);
    memcpy(&in_len, packet + 4, 4);
    memcpy(&out_len, packet + 8, 4);
    assert(in_len == 0 && out_len == 4);

    puts("dcp_iomfb_rpc_test: ok");
    return 0;
}
