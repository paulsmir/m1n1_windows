/* SPDX-License-Identifier: MIT */

#include "hv_autonomous_manifest.h"

#include <stdint.h>

static bool fail(enum hv_autonomous_error value, enum hv_autonomous_error *error)
{
    if (error)
        *error = value;
    return false;
}

static bool magic_matches(const uint8_t magic[8])
{
    static const uint8_t expected[8] = HV_AUTONOMOUS_MAGIC;

    for (size_t i = 0; i < sizeof(expected); i++) {
        if (magic[i] != expected[i])
            return false;
    }
    return true;
}

bool hv_autonomous_manifest_parse(const void *image_end, size_t available,
                                  struct hv_autonomous_payload *out,
                                  enum hv_autonomous_error *error)
{
    const struct hv_autonomous_manifest *manifest = image_end;

    if (!error)
        return false;
    *error = HV_AUTONOMOUS_ERROR_NONE;
    if (!manifest || !out)
        return fail(HV_AUTONOMOUS_ERROR_NULL, error);
    if (available < sizeof(*manifest))
        return fail(HV_AUTONOMOUS_ERROR_TRUNCATED, error);
    if (!magic_matches(manifest->magic))
        return fail(HV_AUTONOMOUS_ERROR_MAGIC, error);
    if (manifest->header_size != sizeof(*manifest))
        return fail(HV_AUTONOMOUS_ERROR_HEADER_SIZE, error);
    if (manifest->format_version != HV_AUTONOMOUS_FORMAT_VERSION)
        return fail(HV_AUTONOMOUS_ERROR_VERSION, error);
    if (manifest->flags || manifest->reserved || manifest->reserved2)
        return fail(HV_AUTONOMOUS_ERROR_FLAGS, error);
    for (size_t i = 0; i < sizeof(manifest->reserved_tail); i++) {
        if (manifest->reserved_tail[i])
            return fail(HV_AUTONOMOUS_ERROR_FLAGS, error);
    }
    if (manifest->layout_version != 1)
        return fail(HV_AUTONOMOUS_ERROR_LAYOUT_VERSION, error);
    if (manifest->payload_offset < manifest->header_size ||
        manifest->payload_offset % HV_AUTONOMOUS_IMAGE_ALIGNMENT)
        return fail(HV_AUTONOMOUS_ERROR_PAYLOAD_ALIGNMENT, error);
    if (!manifest->compressed_size || !manifest->uncompressed_size)
        return fail(HV_AUTONOMOUS_ERROR_PAYLOAD_SIZE, error);
    if (manifest->payload_offset > UINT64_MAX - manifest->compressed_size)
        return fail(HV_AUTONOMOUS_ERROR_INTEGER_OVERFLOW, error);
    if (manifest->payload_offset > SIZE_MAX || manifest->compressed_size > SIZE_MAX ||
        manifest->uncompressed_size > SIZE_MAX)
        return fail(HV_AUTONOMOUS_ERROR_INTEGER_OVERFLOW, error);
    if ((size_t)manifest->payload_offset > available ||
        (size_t)manifest->compressed_size > available - (size_t)manifest->payload_offset)
        return fail(HV_AUTONOMOUS_ERROR_PAYLOAD_BOUNDS, error);

    out->compressed = (const uint8_t *)image_end + (size_t)manifest->payload_offset;
    out->compressed_size = (size_t)manifest->compressed_size;
    out->uncompressed_size = (size_t)manifest->uncompressed_size;
    out->crc32 = manifest->crc32;
    out->layout_version = manifest->layout_version;
    out->flags = manifest->flags;
    return true;
}
