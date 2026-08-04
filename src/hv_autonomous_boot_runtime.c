/* SPDX-License-Identifier: MIT */

#include "hv_autonomous_boot_runtime.h"

#include "hv_autonomous_boot.h"
#include "hv_autonomous_manifest.h"
#include "hv_autonomous_profile.h"
#include "iodev.h"
#include "types.h"
#include "uartproxy.h"
#include "usb.h"
#include "utils.h"

#define HV_AUTONOMOUS_MAX_IMAGE_SIZE (64u * 1024u * 1024u)
#define HV_AUTONOMOUS_WINDOW_SECONDS 3u

struct boot_runtime_io {
    bool proxy_ready;
};

static u64 runtime_now(void *opaque)
{
    UNUSED(opaque);
    return mrs(CNTPCT_EL0);
}

static void runtime_service(void *opaque)
{
    struct boot_runtime_io *io = opaque;

    for (int index = 0; index < USB_IODEV_COUNT; index++) {
        iodev_id_t iodev = IODEV_USB0 + index;
        if (!(iodev_get_usage(iodev) & USAGE_UARTPROXY))
            continue;
        iodev_handle_events(iodev);
        if (iodev_can_write(iodev) || iodev_can_write(IODEV_USB_VUART))
            io->proxy_ready = true;
    }
}

static enum hv_autonomous_command runtime_command(void *opaque)
{
    struct boot_runtime_io *io = opaque;
    return io->proxy_ready ? HV_AUTONOMOUS_COMMAND_PROXY : HV_AUTONOMOUS_COMMAND_NONE;
}

static bool runtime_launch(const struct hv_autonomous_payload *payload,
                           struct hv_autonomous_status *status, void *opaque)
{
    UNUSED(opaque);
    return hv_autonomous_prepare(payload, status) == HV_AUTONOMOUS_RESULT_OK;
}

static void runtime_proxy(void *opaque)
{
    UNUSED(opaque);
    printf("Standalone: debug host detected, transferring control to proxy\n");
    uartproxy_run(NULL);
}

enum hv_autonomous_boot_attempt hv_autonomous_boot_if_present(bool *usb_up)
{
    struct hv_autonomous_payload payload;
    struct hv_autonomous_status status = {0};
    enum hv_autonomous_error manifest_error;
    struct hv_autonomous_profile profile;
    struct boot_runtime_io io = {0};
    const struct hv_autonomous_boot_ops ops = {
        .now = runtime_now,
        .command = runtime_command,
        .service = runtime_service,
        .launch = runtime_launch,
        .proxy = runtime_proxy,
    };

    if (!hv_autonomous_manifest_parse(_payload_start, HV_AUTONOMOUS_MAX_IMAGE_SIZE, &payload,
                                      &manifest_error)) {
        if (manifest_error != HV_AUTONOMOUS_ERROR_MAGIC)
            printf("Standalone: invalid manifest error=%u\n", manifest_error);
        return manifest_error == HV_AUTONOMOUS_ERROR_MAGIC ? HV_AUTONOMOUS_BOOT_ABSENT
                                                           : HV_AUTONOMOUS_BOOT_ATTEMPT_FAILED;
    }

    printf("Standalone: image valid layout=%u compressed=0x%lx firmware=0x%lx\n",
           payload.layout_version, (u64)payload.compressed_size, (u64)payload.uncompressed_size);

    if (!hv_autonomous_profile_decode(payload.flags, &profile)) {
        printf("Standalone: invalid launch profile flags=%#x\n", payload.flags);
        return HV_AUTONOMOUS_BOOT_ATTEMPT_FAILED;
    }

    /* A physical/headless production profile has no host-side consumer. Avoid
     * bringing up USB and avoid the three-second proxy window entirely. A
     * virtual display still needs the USB link even when debug capture is off. */
    if (!profile.debug_host && !profile.virtual_display) {
        printf("Standalone: quiet automatic Windows entry (flags=%#x)\n", payload.flags);
        if (hv_autonomous_prepare(&payload, &status) == HV_AUTONOMOUS_RESULT_OK)
            return HV_AUTONOMOUS_BOOT_HANDLED;
        printf("Standalone: launch returned stage=%u error=%u\n", status.stage, status.error);
        return HV_AUTONOMOUS_BOOT_ATTEMPT_FAILED;
    }

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

    u64 deadline = mrs(CNTFRQ_EL0) * HV_AUTONOMOUS_WINDOW_SECONDS;
    printf("Standalone: automatic Windows entry in %u seconds (attach debug host to hold)\n",
           HV_AUTONOMOUS_WINDOW_SECONDS);
    enum hv_autonomous_boot_result result =
        hv_autonomous_boot_poll(deadline, &ops, &payload, &status, &io);

    if (result == HV_AUTONOMOUS_BOOT_PROXY)
        return HV_AUTONOMOUS_BOOT_HANDLED;
    printf("Standalone: launch returned result=%u stage=%u error=%u\n", result, status.stage,
           status.error);
    return HV_AUTONOMOUS_BOOT_ATTEMPT_FAILED;
}
