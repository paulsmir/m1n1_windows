/* SPDX-License-Identifier: MIT */

#ifndef HV_AUTONOMOUS_MANIFEST_H
#define HV_AUTONOMOUS_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HV_AUTONOMOUS_MAGIC "ASIWINGU"
#define HV_AUTONOMOUS_FORMAT_VERSION 1u
#define HV_AUTONOMOUS_IMAGE_ALIGNMENT 0x4000u
#define HV_AUTONOMOUS_MANIFEST_SIZE 64u

#define HV_AUTONOMOUS_DISPLAY_MASK     0x3u
#define HV_AUTONOMOUS_DISPLAY_PHYSICAL 0x1u
#define HV_AUTONOMOUS_DISPLAY_VIRTUAL  0x2u
#define HV_AUTONOMOUS_DEBUG_MASK       0xcu
#define HV_AUTONOMOUS_DEBUG_UART       0x4u
#define HV_AUTONOMOUS_DEBUG_FULL       0x8u
#define HV_AUTONOMOUS_KNOWN_FLAGS      0xfu

/*
 * This header begins at m1n1's _payload_start. All multibyte fields are
 * little-endian. payload_offset is relative to the start of this structure,
 * which keeps the ABI independent of the size of a particular m1n1 build.
 */
struct hv_autonomous_manifest {
    uint8_t magic[8];
    uint16_t format_version;
    uint16_t header_size;
    uint32_t flags;
    uint32_t layout_version;
    uint32_t reserved;
    uint64_t payload_offset;
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    uint32_t crc32;
    uint32_t reserved2;
    uint8_t reserved_tail[8];
};

_Static_assert(sizeof(struct hv_autonomous_manifest) == HV_AUTONOMOUS_MANIFEST_SIZE,
               "autonomous manifest ABI size changed");

enum hv_autonomous_error {
    HV_AUTONOMOUS_ERROR_NONE = 0,
    HV_AUTONOMOUS_ERROR_NULL,
    HV_AUTONOMOUS_ERROR_TRUNCATED,
    HV_AUTONOMOUS_ERROR_MAGIC,
    HV_AUTONOMOUS_ERROR_HEADER_SIZE,
    HV_AUTONOMOUS_ERROR_VERSION,
    HV_AUTONOMOUS_ERROR_FLAGS,
    HV_AUTONOMOUS_ERROR_LAYOUT_VERSION,
    HV_AUTONOMOUS_ERROR_PAYLOAD_ALIGNMENT,
    HV_AUTONOMOUS_ERROR_PAYLOAD_SIZE,
    HV_AUTONOMOUS_ERROR_INTEGER_OVERFLOW,
    HV_AUTONOMOUS_ERROR_PAYLOAD_BOUNDS,
};

struct hv_autonomous_payload {
    const void *compressed;
    size_t compressed_size;
    size_t uncompressed_size;
    uint32_t crc32;
    uint32_t layout_version;
    uint32_t flags;
};

bool hv_autonomous_manifest_parse(const void *image_end, size_t available,
                                  struct hv_autonomous_payload *out,
                                  enum hv_autonomous_error *error);

#endif
