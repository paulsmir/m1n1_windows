#include <assert.h>
#include <stdio.h>

#include "../src/hv_apple_input.h"

struct backend_trace {
    unsigned route_checks;
    unsigned maps;
    unsigned route_registers;
    unsigned writes;
    unsigned fail_map_at;
    bool route_available;
    u64 mapped_base[3];
    u64 mapped_size[3];
};

static bool route_available(void *context, u32 parent_irq, u32 guest_vintid)
{
    struct backend_trace *trace = context;
    trace->route_checks++;
    assert(parent_irq == HV_APPLE_INPUT_PHYSICAL_PARENT_IRQ);
    assert(guest_vintid == HV_APPLE_INPUT_GUEST_VINTID);
    return trace->route_available;
}

static bool map_identity(void *context, u64 base, u64 size)
{
    struct backend_trace *trace = context;
    unsigned call = ++trace->maps;
    if (call <= 3) {
        trace->mapped_base[call - 1] = base;
        trace->mapped_size[call - 1] = size;
    }
    return call != trace->fail_map_at;
}

static bool register_route(void *context, u32 parent_irq, u32 guest_vintid, bool level)
{
    struct backend_trace *trace = context;
    trace->route_registers++;
    assert(parent_irq == HV_APPLE_INPUT_PHYSICAL_PARENT_IRQ);
    assert(guest_vintid == HV_APPLE_INPUT_GUEST_VINTID);
    assert(level);
    return true;
}

static struct hv_apple_input_prepare_backend backend(struct backend_trace *trace)
{
    return (struct hv_apple_input_prepare_backend){
        .context = trace,
        .route_available = route_available,
        .map_identity = map_identity,
        .register_level_route = register_route,
    };
}

static struct hv_apple_input_observed valid_observation(void)
{
    struct hv_apple_input_observed value = {
        .spi_base = HV_APPLE_INPUT_SPI_BASE,
        .spi_size = HV_APPLE_INPUT_SPI_SIZE,
        .spi_source_hz = HV_APPLE_INPUT_SPI_SOURCE_HZ,
        .spi_bus_hz = HV_APPLE_INPUT_SPI_BUS_HZ,
        .ap_gpio_base = HV_APPLE_INPUT_AP_GPIO_BASE,
        .ap_gpio_size = HV_APPLE_INPUT_AP_GPIO_SIZE,
        .ap_gpio_pin = HV_APPLE_INPUT_AP_GPIO_PIN,
        .nub_gpio_base = HV_APPLE_INPUT_NUB_GPIO_BASE,
        .nub_gpio_size = HV_APPLE_INPUT_NUB_GPIO_SIZE,
        .nub_gpio_pin = HV_APPLE_INPUT_NUB_GPIO_PIN,
        .parent_irq = HV_APPLE_INPUT_PHYSICAL_PARENT_IRQ,
        .spi_compatible = true,
        .hid_compatible = true,
    };
    return value;
}

int main(void)
{
    struct hv_apple_input_observed observed = valid_observation();
    struct hv_apple_input_preflight result;

    assert(hv_apple_input_validate(&observed, &result));
    assert(result.ready);
    assert(result.failure == HV_APPLE_INPUT_PREFLIGHT_OK);
    assert(result.parent_irq == HV_APPLE_INPUT_PHYSICAL_PARENT_IRQ);
    assert(result.guest_vintid == HV_APPLE_INPUT_GUEST_VINTID);

    observed = valid_observation();
    observed.parent_irq = 333;
    assert(!hv_apple_input_validate(&observed, &result));
    assert(result.failure == HV_APPLE_INPUT_PREFLIGHT_PARENT_IRQ);

    observed = valid_observation();
    observed.spi_size--;
    assert(!hv_apple_input_validate(&observed, &result));
    assert(!result.ready);
    assert(result.failure == HV_APPLE_INPUT_PREFLIGHT_SPI_SIZE);

    observed = valid_observation();
    observed.parent_irq = 337;
    assert(!hv_apple_input_validate(&observed, &result));
    assert(result.failure == HV_APPLE_INPUT_PREFLIGHT_PARENT_IRQ);

    observed = valid_observation();
    observed.hid_compatible = false;
    assert(!hv_apple_input_validate(&observed, &result));
    assert(result.failure == HV_APPLE_INPUT_PREFLIGHT_HID_COMPATIBLE);

    assert(!hv_apple_input_validate(NULL, &result));
    assert(!hv_apple_input_validate(&observed, NULL));

    /* Validation failure is fail-closed: no route query, mapping or write. */
    observed = valid_observation();
    observed.spi_base++;
    struct backend_trace trace = {.route_available = true};
    struct hv_apple_input_prepare_backend ops = backend(&trace);
    struct hv_apple_input_prepare_result prepared;
    assert(!hv_apple_input_prepare(&observed, &ops, &prepared));
    assert(prepared.failure == HV_APPLE_INPUT_PREPARE_PREFLIGHT);
    assert(trace.route_checks == 0 && trace.maps == 0 && trace.route_registers == 0);
    assert(trace.writes == 0);

    /* ADT function-* is phandle + FourCC + argument words. */
    const unsigned char gpio_function[] = {
        0x6b, 0, 0, 0, 'g', 'p', 'i', 'o', 0xc3, 0, 0, 0, 0, 0, 0, 0,
    };
    u32 pin = 0;
    assert(hv_apple_input_decode_gpio_function(gpio_function, sizeof(gpio_function),
                                               0x6b, &pin));
    assert(pin == HV_APPLE_INPUT_AP_GPIO_PIN);
    assert(!hv_apple_input_decode_gpio_function(gpio_function, sizeof(gpio_function),
                                                0x6c, &pin));
    assert(!hv_apple_input_decode_gpio_function(gpio_function, 8, 0x6b, &pin));

    const u32 parents[] = HV_APPLE_INPUT_PARENT_IRQ_VALUES;
    u32 parent = 0;
    assert(hv_apple_input_select_parent_irq(parents, HV_APPLE_INPUT_PARENT_IRQ_COUNT,
                                            HV_APPLE_INPUT_IRQ_STARTUP_GROUP, &parent));
    assert(parent == HV_APPLE_INPUT_PHYSICAL_PARENT_IRQ);
    assert(!hv_apple_input_select_parent_irq(parents, HV_APPLE_INPUT_PARENT_IRQ_COUNT,
                                             HV_APPLE_INPUT_PARENT_IRQ_COUNT, &parent));

    /* A route collision is detected before any stage-2 mapping. */
    observed = valid_observation();
    trace = (struct backend_trace){.route_available = false};
    ops = backend(&trace);
    assert(!hv_apple_input_prepare(&observed, &ops, &prepared));
    assert(prepared.failure == HV_APPLE_INPUT_PREPARE_ROUTE_CONFLICT);
    assert(trace.route_checks == 1 && trace.maps == 0 && trace.route_registers == 0);

    /* Identity mappings are bounded to the three contract regions. */
    trace = (struct backend_trace){.route_available = true};
    ops = backend(&trace);
    assert(hv_apple_input_prepare(&observed, &ops, &prepared));
    assert(prepared.ready && prepared.failure == HV_APPLE_INPUT_PREPARE_OK);
    assert(trace.maps == 3 && trace.route_registers == 1 && trace.writes == 0);
    assert(trace.mapped_base[0] == HV_APPLE_INPUT_SPI_BASE);
    assert(trace.mapped_size[0] == HV_APPLE_INPUT_SPI_SIZE);
    assert(trace.mapped_base[1] == HV_APPLE_INPUT_AP_GPIO_BASE);
    assert(trace.mapped_size[1] == HV_APPLE_INPUT_AP_GPIO_SIZE);
    assert(trace.mapped_base[2] == HV_APPLE_INPUT_NUB_GPIO_BASE);
    assert(trace.mapped_size[2] == HV_APPLE_INPUT_NUB_GPIO_SIZE);

    /* A failed map never publishes or registers a guest interrupt route. */
    trace = (struct backend_trace){.route_available = true, .fail_map_at = 2};
    ops = backend(&trace);
    assert(!hv_apple_input_prepare(&observed, &ops, &prepared));
    assert(prepared.failure == HV_APPLE_INPUT_PREPARE_MAP_AP_GPIO);
    assert(!prepared.ready && trace.maps == 2 && trace.route_registers == 0);

    puts("hv_apple_input_contract_test: ok");
    return 0;
}
