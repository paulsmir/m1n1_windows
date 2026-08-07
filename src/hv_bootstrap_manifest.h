/* SPDX-License-Identifier: MIT */

#ifndef HV_BOOTSTRAP_MANIFEST_H
#define HV_BOOTSTRAP_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hv_autonomous_manifest.h"

#define HV_BOOTSTRAP_MAGIC "ASIBOOT0"
#define HV_BOOTSTRAP_FORMAT_VERSION 1u
#define HV_BOOTSTRAP_IMAGE_ALIGNMENT 0x4000u
#define HV_BOOTSTRAP_MANIFEST_SIZE 64u

/*
 * This header begins at Stage 0 m1n1's _payload_start. All multibyte fields
 * are little-endian. The compressed payload is the complete Stage 1 image.
 */
struct __attribute__((packed)) hv_bootstrap_manifest {
    uint8_t magic[8];
    uint16_t format_version;
    uint16_t header_size;
    uint32_t flags;
    uint32_t reserved;
    uint64_t payload_offset;
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    uint32_t crc32;
    uint32_t reserved2;
    uint8_t reserved_tail[12];
};

_Static_assert(sizeof(struct hv_bootstrap_manifest) == HV_BOOTSTRAP_MANIFEST_SIZE,
               "bootstrap manifest ABI size changed");

enum hv_bootstrap_error {
    HV_BOOTSTRAP_ERROR_NONE = 0,
    HV_BOOTSTRAP_ERROR_NULL,
    HV_BOOTSTRAP_ERROR_TRUNCATED,
    HV_BOOTSTRAP_ERROR_MAGIC,
    HV_BOOTSTRAP_ERROR_HEADER_SIZE,
    HV_BOOTSTRAP_ERROR_VERSION,
    HV_BOOTSTRAP_ERROR_FLAGS,
    HV_BOOTSTRAP_ERROR_RESERVED,
    HV_BOOTSTRAP_ERROR_PAYLOAD_ALIGNMENT,
    HV_BOOTSTRAP_ERROR_PAYLOAD_SIZE,
    HV_BOOTSTRAP_ERROR_INTEGER_OVERFLOW,
    HV_BOOTSTRAP_ERROR_PAYLOAD_BOUNDS,
};

struct hv_bootstrap_payload {
    const void *compressed;
    size_t compressed_size;
    size_t uncompressed_size;
    uint32_t crc32;
    uint32_t flags;
};

bool hv_bootstrap_manifest_parse(const void *image_end, size_t available,
                                 struct hv_bootstrap_payload *out,
                                 enum hv_bootstrap_error *error);

#endif
