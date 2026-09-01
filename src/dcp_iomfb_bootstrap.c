/* SPDX-License-Identifier: MIT */

#include "dcp_iomfb_bootstrap.h"
#include "dcp_iomfb_v13_5_abi.h"

#include <string.h>

static void store_u32(void *output, uint32_t value)
{
    memcpy(output, &value, sizeof(value));
}

static uint32_t load_u32(const void *input)
{
    uint32_t value;

    memcpy(&value, input, sizeof(value));
    return value;
}

static bool parse_callback_id(const char tag[4], unsigned int *id)
{
    unsigned int value = 0;
    unsigned int i;

    if (!tag || tag[0] != 'D' || !id)
        return false;
    for (i = 1; i < 4; i++) {
        if (tag[i] < '0' || tag[i] > '9')
            return false;
        value = value * 10 + (unsigned int)(tag[i] - '0');
    }
    *id = value;
    return true;
}

static bool call_method(struct dcp_iomfb_bootstrap *bootstrap,
                        unsigned int id, const char tag[4], const void *input,
                        void *output)
{
    struct dcp_iomfb_abi_size size;

    if (!dcp_iomfb_v13_5_method_size(id, &size))
        return false;
    return bootstrap->call(bootstrap->opaque, tag, input, size.input, output,
                           size.output);
}

static int fail(struct dcp_iomfb_bootstrap *bootstrap)
{
    bootstrap->state = DCP_IOMFB_BOOT_FAILED;
    return -1;
}

void dcp_iomfb_bootstrap_init(struct dcp_iomfb_bootstrap *bootstrap,
                              dcp_iomfb_boot_call_fn call,
                              dcp_iomfb_boot_platform_fn platform,
                              void *opaque)
{
    memset(bootstrap, 0, sizeof(*bootstrap));
    bootstrap->call = call;
    bootstrap->platform = platform;
    bootstrap->opaque = opaque;
    bootstrap->state = DCP_IOMFB_BOOT_OFF;
}

bool dcp_iomfb_bootstrap_start_through_color_remap(
    struct dcp_iomfb_bootstrap *bootstrap)
{
    uint32_t result = 0;
    uint32_t mode[2] = {6, 0};
    uint32_t mode_result[2] = {0, 0};

    if (!bootstrap || !bootstrap->call ||
        bootstrap->state == DCP_IOMFB_BOOT_FAILED)
        return false;

    bootstrap->state = DCP_IOMFB_BOOT_BOOTSTRAP;
    if (!call_method(bootstrap, 401, "A401", NULL, &result))
        goto failed;

    bootstrap->state = DCP_IOMFB_BOOT_POST_INIT;
    if (!call_method(bootstrap, 426, "A426", mode, mode_result))
        goto failed;
    return true;

failed:
    bootstrap->state = DCP_IOMFB_BOOT_FAILED;
    return false;
}

bool dcp_iomfb_bootstrap_start(struct dcp_iomfb_bootstrap *bootstrap)
{
    uint32_t result = 0;

    if (!dcp_iomfb_bootstrap_start_through_first_client_open(bootstrap))
        return false;
    if (!call_method(bootstrap, 411, "A411", NULL, &result))
        goto failed;

    bootstrap->main_display = result != 0;
    bootstrap->state = DCP_IOMFB_BOOT_ACTIVE;
    return true;

failed:
    bootstrap->state = DCP_IOMFB_BOOT_FAILED;
    return false;
}

bool dcp_iomfb_bootstrap_power_on(struct dcp_iomfb_bootstrap *bootstrap)
{
    if (!bootstrap || !bootstrap->call || !bootstrap->main_display)
        return false;

    if (bootstrap->state == DCP_IOMFB_BOOT_ACTIVE &&
        !dcp_iomfb_bootstrap_power_on_firmware(bootstrap))
        return false;
    return dcp_iomfb_bootstrap_select_display(bootstrap);
}

bool dcp_iomfb_bootstrap_power_on_firmware(
    struct dcp_iomfb_bootstrap *bootstrap)
{
    uint8_t power_input[12] = {0};
    uint8_t power_output[8] = {0};

    if (!bootstrap || !bootstrap->call || !bootstrap->main_display ||
        bootstrap->state != DCP_IOMFB_BOOT_ACTIVE)
        return false;

    /* The pinned integrated-panel path calls setPowerState(1, false, &result)
     * before selecting display device zero. The nullable output marker at
     * byte 9 is zero because the result pointer is present. */
    store_u32(power_input, 1);
    if (!call_method(bootstrap, 472, "A472", power_input, power_output) ||
        load_u32(power_output + sizeof(uint32_t)) != 0) {
        bootstrap->state = DCP_IOMFB_BOOT_FAILED;
        return false;
    }

    bootstrap->state = DCP_IOMFB_BOOT_POWER_ON;
    return true;
}

bool dcp_iomfb_bootstrap_select_display(
    struct dcp_iomfb_bootstrap *bootstrap)
{
    uint32_t display_device = 0;
    uint32_t display_result = 0;

    if (!bootstrap || !bootstrap->call || !bootstrap->main_display ||
        bootstrap->state != DCP_IOMFB_BOOT_POWER_ON)
        return false;

    if (!call_method(bootstrap, 410, "A410", &display_device,
                     &display_result) ||
        display_result != 2) {
        bootstrap->state = DCP_IOMFB_BOOT_FAILED;
        return false;
    }

    bootstrap->state = DCP_IOMFB_BOOT_POWERED;
    return true;
}

bool dcp_iomfb_bootstrap_modeset(struct dcp_iomfb_bootstrap *bootstrap,
                                 uint32_t color_mode_id,
                                 uint32_t timing_mode_id)
{
    uint32_t input[2] = {color_mode_id, timing_mode_id};
    uint32_t result = 0;

    if (!bootstrap || !bootstrap->call ||
        bootstrap->state != DCP_IOMFB_BOOT_POWERED)
        return false;
    if (!call_method(bootstrap, 412, "A412", input, &result) || result != 0) {
        bootstrap->state = DCP_IOMFB_BOOT_FAILED;
        return false;
    }

    bootstrap->state = DCP_IOMFB_BOOT_MODESET;
    return true;
}

bool dcp_iomfb_bootstrap_start_through_video_power_savings(
    struct dcp_iomfb_bootstrap *bootstrap)
{
    uint32_t result = 0;
    uint32_t zero = 0;

    if (!dcp_iomfb_bootstrap_start_through_color_remap(bootstrap))
        return false;
    if (!call_method(bootstrap, 449, "A449", &zero, &result)) {
        bootstrap->state = DCP_IOMFB_BOOT_FAILED;
        return false;
    }
    return true;
}

bool dcp_iomfb_bootstrap_start_through_first_client_open(
    struct dcp_iomfb_bootstrap *bootstrap)
{
    if (!dcp_iomfb_bootstrap_start_through_video_power_savings(bootstrap))
        return false;
    if (!call_method(bootstrap, 456, "A456", NULL, NULL)) {
        bootstrap->state = DCP_IOMFB_BOOT_FAILED;
        return false;
    }
    return true;
}

int dcp_iomfb_bootstrap_callback(struct dcp_iomfb_bootstrap *bootstrap,
                                 const char tag[4], const void *input,
                                 uint32_t input_size, void *output,
                                 uint32_t output_size)
{
    struct dcp_iomfb_abi_size expected;
    unsigned int id;
    uint32_t value = 0;
    uint32_t one = 1;

    if (!bootstrap || !parse_callback_id(tag, &id) ||
        !dcp_iomfb_v13_5_callback_size(id, &expected) ||
        expected.input != input_size || expected.output != output_size ||
        (input_size && !input) || (output_size && !output))
        return bootstrap ? fail(bootstrap) : -1;

    if (output_size)
        memset(output, 0, output_size);

    switch (id) {
    case 0:
    case 1:
    case 108:
    case 109:
    case 110:
    case 111:
    case 113:
    case 582:
        store_u32(output, 1);
        return 0;
    case 112:
        /* J313/13.5 create_backlight_service: no host-side service exists. */
        store_u32(output, 0);
        return 0;
    case 2:
    case 6:
    case 102:
    case 103:
    case 104:
    case 107:
    case 116:
    case 208:
    case 300:
    case 404:
    case 406:
    case 577:
    case 579:
    case 581:
    case 584:
    case 588:
    case 591:
    case 592:
    case 593:
    case 594:
    case 598:
        return 0;
    case 101:
        store_u32(output, 0);
        return 0;
    case 126:
    case 127:
    case 128:
    case 201:
    case 202:
    case 209:
    case 408:
    case 411:
    case 451:
    case 452:
    case 453:
    case 454:
    case 455:
    case 456:
        if (!bootstrap->platform ||
            bootstrap->platform(bootstrap->opaque, id, input, input_size,
                                output, output_size) != 0)
            return fail(bootstrap);
        return 0;
    case 576:
        /* Firmware 13.5 hotPlug_notify_gated carries a uint followed by an
         * optional 0x4c-byte in/out object.  The canonical AP callback is a
         * notification and leaves a present object unchanged.  A null object
         * is valid and retains the zero reply prepared above. */
        if (((const uint8_t *)input)[0x50] == 0)
            memcpy(output, (const uint8_t *)input + sizeof(uint32_t),
                   output_size);
        return 0;
    case 3:
        if (!bootstrap->platform ||
            bootstrap->platform(bootstrap->opaque, id, input, input_size,
                                output, output_size) != 0)
            return fail(bootstrap);
        return 0;
    case 100:
        if (!call_method(bootstrap, 374, "A374", NULL, &value))
            return fail(bootstrap);
        return 0;
    case 114:
        store_u32(output, 0);
        store_u32((uint8_t *)output + 4, 1);
        return 0;
    case 124:
        /* Preserve the complete eight-word in/out EDT value while truthfully
         * reporting that no requested property was found. */
        memcpy(output, (const uint8_t *)input + 0x44, 8 * sizeof(uint32_t));
        ((uint8_t *)output)[0x20] = 0;
        return 0;
    case 129:
        memcpy(output, input, 16);
        store_u32((uint8_t *)output + 16, 1);
        return 0;
    case 413:
    case 414:
    case 415:
    case 552:
    case 561:
    case 563:
    case 565:
    case 567:
        store_u32(output, 1);
        return 0;
    case 401:
        /* Unknown scalar service property: value zero, not found. */
        return 0;
    case 575:
    case 578:
    case 583:
        /* Optional query/debug services remain truthfully unsupported. */
        return 0;
    case 115:
    case 121:
    case 122:
    case 596:
    case 597:
        store_u32(output, 0);
        return 0;
    case 120:
        if (!call_method(bootstrap, 373, "A373", NULL, NULL) ||
            !call_method(bootstrap, 445, "A445", NULL, &value) ||
            !call_method(bootstrap, 29, "A029", NULL, NULL) ||
            !call_method(bootstrap, 466, "A466", &one, NULL) ||
            !call_method(bootstrap, 0, "A000", &one, &value) ||
            !call_method(bootstrap, 463, "A463", NULL, &value))
            return fail(bootstrap);
        store_u32(output, 1);
        return 0;
    case 206:
        if (!call_method(bootstrap, 131, "A131", NULL, &value))
            return fail(bootstrap);
        store_u32(output, 1);
        return 0;
    case 207:
        if (!call_method(bootstrap, 132, "A132", NULL, &value))
            return fail(bootstrap);
        store_u32(output, 1);
        return 0;
    case 574:
        store_u32(output, 0);
        return 0;
    case 589:
        /* Keep D589 inside this sole endpoint owner, but let the platform
         * layer apply the strict versioned latch parser and swap-id policy. */
        if (!bootstrap->platform ||
            bootstrap->platform(bootstrap->opaque, id, input, input_size,
                                output, output_size) != 0)
            return fail(bootstrap);
        return 0;
    default:
        return fail(bootstrap);
    }
}

enum dcp_iomfb_boot_state
dcp_iomfb_bootstrap_state(const struct dcp_iomfb_bootstrap *bootstrap)
{
    return bootstrap ? bootstrap->state : DCP_IOMFB_BOOT_FAILED;
}

bool dcp_iomfb_bootstrap_main_display(
    const struct dcp_iomfb_bootstrap *bootstrap)
{
    return bootstrap && bootstrap->main_display;
}
