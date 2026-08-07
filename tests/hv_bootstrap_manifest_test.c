#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/hv_bootstrap_manifest.h"

static struct hv_bootstrap_manifest valid_manifest(void)
{
    struct hv_bootstrap_manifest manifest = {0};
    memcpy(manifest.magic, HV_BOOTSTRAP_MAGIC, sizeof(manifest.magic));
    manifest.format_version = HV_BOOTSTRAP_FORMAT_VERSION;
    manifest.header_size = HV_BOOTSTRAP_MANIFEST_SIZE;
    manifest.flags = HV_AUTONOMOUS_DISPLAY_PHYSICAL |
                     HV_AUTONOMOUS_DEBUG_MONITOR;
    manifest.payload_offset = HV_BOOTSTRAP_IMAGE_ALIGNMENT;
    manifest.compressed_size = 0x8000;
    manifest.uncompressed_size = 0x1e00000;
    manifest.crc32 = 0x12345678;
    return manifest;
}

static void expect_error(struct hv_bootstrap_manifest *manifest, size_t available,
                         enum hv_bootstrap_error expected)
{
    struct hv_bootstrap_payload payload = {0};
    enum hv_bootstrap_error error = HV_BOOTSTRAP_ERROR_NONE;

    assert(!hv_bootstrap_manifest_parse(manifest, available, &payload, &error));
    assert(error == expected);
}

int main(void)
{
    struct hv_bootstrap_manifest manifest = valid_manifest();
    struct hv_bootstrap_payload payload = {0};
    enum hv_bootstrap_error error = HV_BOOTSTRAP_ERROR_MAGIC;
    const size_t available = manifest.payload_offset + manifest.compressed_size;

    assert(hv_bootstrap_manifest_parse(&manifest, available, &payload, &error));
    assert(error == HV_BOOTSTRAP_ERROR_NONE);
    assert(payload.compressed == (const uint8_t *)&manifest + manifest.payload_offset);
    assert(payload.compressed_size == manifest.compressed_size);
    assert(payload.uncompressed_size == manifest.uncompressed_size);
    assert(payload.crc32 == manifest.crc32);
    assert(payload.flags == manifest.flags);

    manifest = valid_manifest();
    manifest.magic[0] ^= 1;
    expect_error(&manifest, available, HV_BOOTSTRAP_ERROR_MAGIC);

    manifest = valid_manifest();
    manifest.header_size--;
    expect_error(&manifest, available, HV_BOOTSTRAP_ERROR_HEADER_SIZE);

    manifest = valid_manifest();
    manifest.format_version++;
    expect_error(&manifest, available, HV_BOOTSTRAP_ERROR_VERSION);

    manifest = valid_manifest();
    manifest.flags = HV_AUTONOMOUS_DEBUG_MASK;
    expect_error(&manifest, available, HV_BOOTSTRAP_ERROR_FLAGS);

    manifest = valid_manifest();
    manifest.payload_offset++;
    expect_error(&manifest, available, HV_BOOTSTRAP_ERROR_PAYLOAD_ALIGNMENT);

    manifest = valid_manifest();
    manifest.compressed_size = 0;
    expect_error(&manifest, available, HV_BOOTSTRAP_ERROR_PAYLOAD_SIZE);

    manifest = valid_manifest();
    manifest.uncompressed_size = 0;
    expect_error(&manifest, available, HV_BOOTSTRAP_ERROR_PAYLOAD_SIZE);

    manifest = valid_manifest();
    expect_error(&manifest, sizeof(manifest) - 1, HV_BOOTSTRAP_ERROR_TRUNCATED);

    manifest = valid_manifest();
    expect_error(&manifest, available - 1, HV_BOOTSTRAP_ERROR_PAYLOAD_BOUNDS);

    manifest = valid_manifest();
    manifest.payload_offset = UINT64_MAX - HV_BOOTSTRAP_IMAGE_ALIGNMENT + 1;
    manifest.compressed_size = HV_BOOTSTRAP_IMAGE_ALIGNMENT;
    expect_error(&manifest, available, HV_BOOTSTRAP_ERROR_INTEGER_OVERFLOW);

    manifest = valid_manifest();
    manifest.reserved = 1;
    expect_error(&manifest, available, HV_BOOTSTRAP_ERROR_RESERVED);

    manifest = valid_manifest();
    manifest.reserved2 = 1;
    expect_error(&manifest, available, HV_BOOTSTRAP_ERROR_RESERVED);

    manifest = valid_manifest();
    manifest.reserved_tail[sizeof(manifest.reserved_tail) - 1] = 1;
    expect_error(&manifest, available, HV_BOOTSTRAP_ERROR_RESERVED);

    assert(!hv_bootstrap_manifest_parse(NULL, available, &payload, &error));
    assert(error == HV_BOOTSTRAP_ERROR_NULL);
    assert(!hv_bootstrap_manifest_parse(&manifest, available, NULL, &error));
    assert(error == HV_BOOTSTRAP_ERROR_NULL);
    assert(!hv_bootstrap_manifest_parse(&manifest, available, &payload, NULL));

    puts("hv_bootstrap_manifest_test: ok");
    return 0;
}
