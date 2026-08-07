/* SPDX-License-Identifier: MIT */

#include "hv_bootstrap.h"

enum hv_bootstrap_result hv_bootstrap_prepare_with_ops(
    const struct hv_bootstrap_payload *payload, const struct hv_bootstrap_ops *ops,
    void *opaque)
{
    uint32_t source_size;
    uint32_t destination_size;
    void *destination;

    if (!payload || !payload->compressed || !payload->compressed_size ||
        !payload->uncompressed_size || payload->compressed_size > UINT32_MAX ||
        payload->uncompressed_size > UINT32_MAX || !ops || !ops->allocate ||
        !ops->decompress || !ops->crc32 || !ops->chainload)
        return HV_BOOTSTRAP_RESULT_INVALID;

    destination = ops->allocate(payload->uncompressed_size, HV_BOOTSTRAP_IMAGE_ALIGNMENT,
                                opaque);
    if (!destination)
        return HV_BOOTSTRAP_RESULT_ALLOCATE_FAILED;

    source_size = payload->compressed_size;
    destination_size = payload->uncompressed_size;
    if (!ops->decompress(payload->compressed, &source_size, destination,
                         &destination_size, opaque))
        return HV_BOOTSTRAP_RESULT_DECOMPRESS_FAILED;
    if (source_size != payload->compressed_size)
        return HV_BOOTSTRAP_RESULT_SOURCE_SIZE;
    if (destination_size != payload->uncompressed_size)
        return HV_BOOTSTRAP_RESULT_DESTINATION_SIZE;
    if (ops->crc32(destination, destination_size, opaque) != payload->crc32)
        return HV_BOOTSTRAP_RESULT_CRC_MISMATCH;
    if (ops->chainload(destination, destination_size, opaque) < 0)
        return HV_BOOTSTRAP_RESULT_CHAINLOAD_FAILED;
    return HV_BOOTSTRAP_RESULT_OK;
}

enum hv_bootstrap_attempt hv_bootstrap_attempt_from_manifest_error(
    enum hv_bootstrap_error error)
{
    return error == HV_BOOTSTRAP_ERROR_MAGIC ? HV_BOOTSTRAP_ABSENT
                                              : HV_BOOTSTRAP_ATTEMPT_FAILED;
}

#ifndef HV_BOOTSTRAP_HOST_TEST

#include "chainload.h"
#include "heapblock.h"
#include "hv_autonomous_profile.h"
#include "iodev.h"
#include "minilzlib/minlzma.h"
#include "tinf/tinf.h"
#include "types.h"
#include "usb.h"
#include "utils.h"

#define HV_BOOTSTRAP_MAX_IMAGE_SIZE (64u * 1024u * 1024u)

static void *runtime_allocate(size_t size, size_t alignment, void *opaque)
{
    UNUSED(opaque);
    return heapblock_alloc_aligned(size, alignment);
}

static bool runtime_decompress(const void *source, uint32_t *source_size, void *destination,
                               uint32_t *destination_size, void *opaque)
{
    UNUSED(opaque);
    printf("BOOTSTRAP_DECOMPRESS source=0x%x destination=0x%x\n", *source_size,
           *destination_size);
    return XzDecode((u8 *)source, source_size, destination, destination_size);
}

static uint32_t runtime_crc32(const void *data, size_t size, void *opaque)
{
    UNUSED(opaque);
    printf("BOOTSTRAP_VERIFY size=0x%lx\n", size);
    return tinf_crc32(data, size);
}

static int runtime_chainload(void *image, size_t size, void *opaque)
{
    UNUSED(opaque);
    printf("BOOTSTRAP_CHAINLOAD size=0x%lx\n", size);
    return chainload_image(image, size, NULL, 0);
}

static void bootstrap_monitor_window(const struct hv_autonomous_profile *profile,
                                     bool *usb_up)
{
    uint32_t seconds = hv_autonomous_profile_usb_window_seconds(profile);
    u64 start;
    u64 duration;

    if (!profile->monitor || !seconds)
        return;
    if (!*usb_up) {
        usb_init();
        usb_iodev_init();
        *usb_up = true;
    }
    for (int index = 0; index < USB_IODEV_COUNT; index++) {
        iodev_id_t iodev = IODEV_USB0 + index;
        if (iodev_get_usage(iodev) & USAGE_UARTPROXY)
            usb_iodev_vuart_setup(iodev);
    }

    printf("BOOTSTRAP_MONITOR passive USB window=%u seconds\n", seconds);
    start = mrs(CNTPCT_EL0);
    duration = mrs(CNTFRQ_EL0) * seconds;
    while (mrs(CNTPCT_EL0) - start < duration) {
        for (int index = 0; index < USB_IODEV_COUNT; index++) {
            iodev_id_t iodev = IODEV_USB0 + index;
            if (iodev_get_usage(iodev) & USAGE_UARTPROXY)
                iodev_handle_events(iodev);
        }
        mdelay(1);
    }
}

enum hv_bootstrap_attempt hv_bootstrap_chainload_if_present(bool *usb_up)
{
    struct hv_bootstrap_payload payload;
    enum hv_bootstrap_error manifest_error;
    struct hv_autonomous_profile profile;
    static const struct hv_bootstrap_ops ops = {
        .allocate = runtime_allocate,
        .decompress = runtime_decompress,
        .crc32 = runtime_crc32,
        .chainload = runtime_chainload,
    };

    if (!usb_up)
        return HV_BOOTSTRAP_ATTEMPT_FAILED;
    if (!hv_bootstrap_manifest_parse(_payload_start, HV_BOOTSTRAP_MAX_IMAGE_SIZE, &payload,
                                     &manifest_error)) {
        enum hv_bootstrap_attempt attempt =
            hv_bootstrap_attempt_from_manifest_error(manifest_error);
        if (attempt != HV_BOOTSTRAP_ABSENT)
            printf("BOOTSTRAP_VALIDATE failed error=%u\n", manifest_error);
        return attempt;
    }
    if (!hv_autonomous_profile_decode(payload.flags, &profile)) {
        printf("BOOTSTRAP_VALIDATE failed flags=%#x\n", payload.flags);
        return HV_BOOTSTRAP_ATTEMPT_FAILED;
    }

    printf("BOOTSTRAP_VALIDATE compressed=0x%lx inner=0x%lx flags=%#x\n",
           (u64)payload.compressed_size, (u64)payload.uncompressed_size, payload.flags);
    bootstrap_monitor_window(&profile, usb_up);

    enum hv_bootstrap_result result = hv_bootstrap_prepare_with_ops(&payload, &ops, NULL);
    if (result != HV_BOOTSTRAP_RESULT_OK) {
        printf("BOOTSTRAP_FAILED result=%u\n", result);
        return HV_BOOTSTRAP_ATTEMPT_FAILED;
    }
    return HV_BOOTSTRAP_HANDLED;
}

#endif
