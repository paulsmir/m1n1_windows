/* SPDX-License-Identifier: MIT */

#ifndef HV_APPLE_INPUT_H
#define HV_APPLE_INPUT_H

#ifdef HV_APPLE_INPUT_HOST_TEST
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include "types.h"
#endif

#include "hv_apple_input.generated.h"

enum hv_apple_input_preflight_failure {
    HV_APPLE_INPUT_PREFLIGHT_OK,
    HV_APPLE_INPUT_PREFLIGHT_NULL,
    HV_APPLE_INPUT_PREFLIGHT_SPI_COMPATIBLE,
    HV_APPLE_INPUT_PREFLIGHT_HID_COMPATIBLE,
    HV_APPLE_INPUT_PREFLIGHT_SPI_BASE,
    HV_APPLE_INPUT_PREFLIGHT_SPI_SIZE,
    HV_APPLE_INPUT_PREFLIGHT_SPI_SOURCE_HZ,
    HV_APPLE_INPUT_PREFLIGHT_SPI_BUS_HZ,
    HV_APPLE_INPUT_PREFLIGHT_AP_GPIO_BASE,
    HV_APPLE_INPUT_PREFLIGHT_AP_GPIO_SIZE,
    HV_APPLE_INPUT_PREFLIGHT_AP_GPIO_PIN,
    HV_APPLE_INPUT_PREFLIGHT_NUB_GPIO_BASE,
    HV_APPLE_INPUT_PREFLIGHT_NUB_GPIO_SIZE,
    HV_APPLE_INPUT_PREFLIGHT_NUB_GPIO_PIN,
    HV_APPLE_INPUT_PREFLIGHT_PARENT_IRQ,
};

struct hv_apple_input_observed {
    u64 spi_base;
    u64 spi_size;
    u64 spi_source_hz;
    u64 spi_bus_hz;
    u64 ap_gpio_base;
    u64 ap_gpio_size;
    u64 ap_gpio_pin;
    u64 nub_gpio_base;
    u64 nub_gpio_size;
    u64 nub_gpio_pin;
    u32 parent_irq;
    bool spi_compatible;
    bool hid_compatible;
};

struct hv_apple_input_preflight {
    bool ready;
    enum hv_apple_input_preflight_failure failure;
    u32 parent_irq;
    u32 guest_vintid;
};

enum hv_apple_input_prepare_failure {
    HV_APPLE_INPUT_PREPARE_OK,
    HV_APPLE_INPUT_PREPARE_NULL,
    HV_APPLE_INPUT_PREPARE_PREFLIGHT,
    HV_APPLE_INPUT_PREPARE_ROUTE_CONFLICT,
    HV_APPLE_INPUT_PREPARE_MAP_SPI,
    HV_APPLE_INPUT_PREPARE_MAP_AP_GPIO,
    HV_APPLE_INPUT_PREPARE_MAP_NUB_GPIO,
    HV_APPLE_INPUT_PREPARE_REGISTER_ROUTE,
};

struct hv_apple_input_prepare_backend {
    void *context;
    bool (*route_available)(void *context, u32 parent_irq, u32 guest_vintid);
    bool (*map_identity)(void *context, u64 base, u64 size);
    bool (*register_level_route)(void *context, u32 parent_irq, u32 guest_vintid,
                                 bool level);
};

struct hv_apple_input_prepare_result {
    bool ready;
    enum hv_apple_input_prepare_failure failure;
    enum hv_apple_input_preflight_failure preflight_failure;
};

bool hv_apple_input_decode_gpio_function(const void *value, u32 length,
                                         u32 *controller_phandle, u32 *pin);
bool hv_apple_input_select_parent_irq(const u32 *parents, size_t parent_count,
                                      u32 group, u32 *parent_irq);

#ifndef HV_APPLE_INPUT_HOST_TEST
bool hv_apple_input_observe_adt(struct hv_apple_input_observed *observed);
bool hv_apple_input_prepare_runtime(struct hv_apple_input_prepare_result *result);
#endif

bool hv_apple_input_validate(const struct hv_apple_input_observed *observed,
                             struct hv_apple_input_preflight *result);
bool hv_apple_input_prepare(const struct hv_apple_input_observed *observed,
                            const struct hv_apple_input_prepare_backend *backend,
                            struct hv_apple_input_prepare_result *result);

#endif
