/* SPDX-License-Identifier: MIT */

#ifndef DCP_IOMFB_LATCH_H
#define DCP_IOMFB_LATCH_H

#ifdef DCP_IOMFB_LATCH_HOST_TEST
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#else
#include "types.h"
#endif

#define DCP_IOMFB_CALLBACK_SWAP_COMPLETE 589u
#define DCP_IOMFB_V12_3_SWAP_COMPLETE_INPUT_SIZE 0x6e0u
#define DCP_IOMFB_V13_5_SWAP_COMPLETE_INPUT_SIZE 0x6f0u

enum dcp_iomfb_protocol_version {
    DCP_IOMFB_PROTOCOL_V12_3 = 0,
    DCP_IOMFB_PROTOCOL_V13_5 = 1,
};

enum dcp_iomfb_latch_result {
    DCP_IOMFB_LATCH_INVALID = -1,
    DCP_IOMFB_LATCH_UNRELATED = 0,
    DCP_IOMFB_LATCH_MATCHED = 1,
    DCP_IOMFB_LATCH_STALE = 2,
};

/* Parse one complete IOMFB callback packet from the shared-memory channel.
 * The packet header uses Apple's reversed four-character callback tag, so the
 * swap-complete callback is encoded as "985D". The payload length is part of
 * the firmware ABI and must match the selected protocol version exactly. */
enum dcp_iomfb_latch_result dcp_iomfb_parse_swap_complete(
    enum dcp_iomfb_protocol_version version, const void *packet,
    size_t packet_size, uint32_t expected_swap_id,
    uint32_t *completed_swap_id);
enum dcp_iomfb_latch_result dcp_iomfb_parse_swap_complete_payload(
    enum dcp_iomfb_protocol_version version, const void *payload,
    size_t payload_size, uint32_t expected_swap_id,
    uint32_t *completed_swap_id);

#endif
