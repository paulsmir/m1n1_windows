/* SPDX-License-Identifier: MIT */

#ifndef HV_AUTONOMOUS_MANIFEST_H
#define HV_AUTONOMOUS_MANIFEST_H

#include <stdint.h>

#define HV_AUTONOMOUS_MAGIC "ASIWINGU"
#define HV_AUTONOMOUS_FORMAT_VERSION 1u
#define HV_AUTONOMOUS_IMAGE_ALIGNMENT 0x4000u
#define HV_AUTONOMOUS_MANIFEST_SIZE 64u

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

#endif
