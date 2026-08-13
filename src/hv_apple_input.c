/* SPDX-License-Identifier: MIT */

#include "hv_apple_input.h"

static bool fail(struct hv_apple_input_preflight *result,
                 enum hv_apple_input_preflight_failure failure)
{
    if (result) {
        result->ready = false;
        result->failure = failure;
        result->parent_irq = 0;
        result->guest_vintid = 0;
    }
    return false;
}

#define REQUIRE_FIELD(member, expected, failure_code) \
    do {                                                \
        if (observed->member != (expected))             \
            return fail(result, (failure_code));        \
    } while (0)

bool hv_apple_input_validate(const struct hv_apple_input_observed *observed,
                             struct hv_apple_input_preflight *result)
{
    if (!observed || !result)
        return fail(result, HV_APPLE_INPUT_PREFLIGHT_NULL);

    result->ready = false;
    result->failure = HV_APPLE_INPUT_PREFLIGHT_OK;
    result->parent_irq = 0;
    result->guest_vintid = 0;

    REQUIRE_FIELD(spi_compatible, true, HV_APPLE_INPUT_PREFLIGHT_SPI_COMPATIBLE);
    REQUIRE_FIELD(hid_compatible, true, HV_APPLE_INPUT_PREFLIGHT_HID_COMPATIBLE);
    REQUIRE_FIELD(spi_base, HV_APPLE_INPUT_SPI_BASE, HV_APPLE_INPUT_PREFLIGHT_SPI_BASE);
    REQUIRE_FIELD(spi_size, HV_APPLE_INPUT_SPI_SIZE, HV_APPLE_INPUT_PREFLIGHT_SPI_SIZE);
    REQUIRE_FIELD(spi_source_hz, HV_APPLE_INPUT_SPI_SOURCE_HZ,
                  HV_APPLE_INPUT_PREFLIGHT_SPI_SOURCE_HZ);
    REQUIRE_FIELD(spi_bus_hz, HV_APPLE_INPUT_SPI_BUS_HZ,
                  HV_APPLE_INPUT_PREFLIGHT_SPI_BUS_HZ);
    REQUIRE_FIELD(ap_gpio_base, HV_APPLE_INPUT_AP_GPIO_BASE,
                  HV_APPLE_INPUT_PREFLIGHT_AP_GPIO_BASE);
    REQUIRE_FIELD(ap_gpio_size, HV_APPLE_INPUT_AP_GPIO_SIZE,
                  HV_APPLE_INPUT_PREFLIGHT_AP_GPIO_SIZE);
    REQUIRE_FIELD(ap_gpio_pin, HV_APPLE_INPUT_AP_GPIO_PIN,
                  HV_APPLE_INPUT_PREFLIGHT_AP_GPIO_PIN);
    REQUIRE_FIELD(nub_gpio_base, HV_APPLE_INPUT_NUB_GPIO_BASE,
                  HV_APPLE_INPUT_PREFLIGHT_NUB_GPIO_BASE);
    REQUIRE_FIELD(nub_gpio_size, HV_APPLE_INPUT_NUB_GPIO_SIZE,
                  HV_APPLE_INPUT_PREFLIGHT_NUB_GPIO_SIZE);
    REQUIRE_FIELD(nub_gpio_pin, HV_APPLE_INPUT_NUB_GPIO_PIN,
                  HV_APPLE_INPUT_PREFLIGHT_NUB_GPIO_PIN);
    if (observed->parent_irq != HV_APPLE_INPUT_PHYSICAL_PARENT_IRQ)
        return fail(result, HV_APPLE_INPUT_PREFLIGHT_PARENT_IRQ);

    result->ready = true;
    result->parent_irq = observed->parent_irq;
    result->guest_vintid = (u32)HV_APPLE_INPUT_GUEST_VINTID;
    return true;
}

static bool prepare_fail(struct hv_apple_input_prepare_result *result,
                         enum hv_apple_input_prepare_failure failure,
                         enum hv_apple_input_preflight_failure preflight_failure)
{
    if (result) {
        result->ready = false;
        result->failure = failure;
        result->preflight_failure = preflight_failure;
    }
    return false;
}

bool hv_apple_input_prepare(const struct hv_apple_input_observed *observed,
                            const struct hv_apple_input_prepare_backend *backend,
                            struct hv_apple_input_prepare_result *result)
{
    struct hv_apple_input_preflight preflight;

    if (!backend || !result || !backend->route_available || !backend->map_identity ||
        !backend->register_level_route)
        return prepare_fail(result, HV_APPLE_INPUT_PREPARE_NULL,
                            HV_APPLE_INPUT_PREFLIGHT_NULL);

    *result = (struct hv_apple_input_prepare_result){0};
    if (!hv_apple_input_validate(observed, &preflight))
        return prepare_fail(result, HV_APPLE_INPUT_PREPARE_PREFLIGHT, preflight.failure);

    /* Route conflicts are reversible to detect; mappings are not.  Therefore
     * no stage-2 state changes before this gate has passed. */
    if (!backend->route_available(backend->context, preflight.parent_irq,
                                  preflight.guest_vintid))
        return prepare_fail(result, HV_APPLE_INPUT_PREPARE_ROUTE_CONFLICT,
                            HV_APPLE_INPUT_PREFLIGHT_OK);

    if (!backend->map_identity(backend->context, observed->spi_base, observed->spi_size))
        return prepare_fail(result, HV_APPLE_INPUT_PREPARE_MAP_SPI,
                            HV_APPLE_INPUT_PREFLIGHT_OK);
    if (!backend->map_identity(backend->context, observed->ap_gpio_base,
                               observed->ap_gpio_size))
        return prepare_fail(result, HV_APPLE_INPUT_PREPARE_MAP_AP_GPIO,
                            HV_APPLE_INPUT_PREFLIGHT_OK);
    if (!backend->map_identity(backend->context, observed->nub_gpio_base,
                               observed->nub_gpio_size))
        return prepare_fail(result, HV_APPLE_INPUT_PREPARE_MAP_NUB_GPIO,
                            HV_APPLE_INPUT_PREFLIGHT_OK);
    if (!backend->register_level_route(backend->context, preflight.parent_irq,
                                       preflight.guest_vintid, true))
        return prepare_fail(result, HV_APPLE_INPUT_PREPARE_REGISTER_ROUTE,
                            HV_APPLE_INPUT_PREFLIGHT_OK);

    result->ready = true;
    result->failure = HV_APPLE_INPUT_PREPARE_OK;
    result->preflight_failure = HV_APPLE_INPUT_PREFLIGHT_OK;
    return true;
}
