/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/dcp_iomfb_properties.h"

struct fixture {
    unsigned allocations;
    unsigned releases;
};

static void *test_allocate(void *opaque, size_t size)
{
    struct fixture *fixture = opaque;
    fixture->allocations++;
    return calloc(1, size);
}

static void test_release(void *opaque, void *pointer)
{
    struct fixture *fixture = opaque;
    fixture->releases++;
    free(pointer);
}

static uint32_t load_u32(const void *pointer)
{
    uint32_t value;
    memcpy(&value, pointer, sizeof(value));
    return value;
}

int main(void)
{
    static const struct dcp_iomfb_property_ops ops = {
        .allocate = test_allocate,
        .release = test_release,
    };
    struct dcp_iomfb_properties properties;
    struct fixture fixture = {0};
    uint8_t input[0x1008] = {0};
    uint8_t output[4] = {0};
    const void *data;
    size_t size;
    uint32_t value;

    dcp_iomfb_properties_init(&properties, &ops, &fixture);

    /* Firmware includes one non-payload terminator in the start length. */
    value = 7;
    memcpy(input, &value, sizeof(value));
    assert(dcp_iomfb_properties_callback(&properties, 126, input, 4,
                                         output, 4) == 0);
    assert(load_u32(output) == 1);

    memcpy(input, "ABC", 3);
    value = 0;
    memcpy(input + 0x1000, &value, 4);
    value = 3;
    memcpy(input + 0x1004, &value, 4);
    memset(output, 0, sizeof(output));
    assert(dcp_iomfb_properties_callback(&properties, 127, input, 0x1008,
                                         output, 4) == 0);
    assert(load_u32(output) == 1);

    memcpy(input, "DEF", 3);
    value = 3;
    memcpy(input + 0x1000, &value, 4);
    value = 3;
    memcpy(input + 0x1004, &value, 4);
    memset(output, 0, sizeof(output));
    assert(dcp_iomfb_properties_callback(&properties, 127, input, 0x1008,
                                         output, 4) == 0);
    assert(load_u32(output) == 1);

    memset(input, 0, 0x40);
    memcpy(input, "TimingElements", 14);
    memset(output, 0, sizeof(output));
    assert(dcp_iomfb_properties_callback(&properties, 128, input, 0x40,
                                         output, 4) == 0);
    assert(load_u32(output) == 1);
    assert(dcp_iomfb_properties_find(&properties, "TimingElements",
                                     &data, &size));
    assert(size == 6 && memcmp(data, "ABCDEF", 6) == 0);

    value = 4;
    memcpy(input, &value, 4);
    assert(dcp_iomfb_properties_callback(&properties, 126, input, 4,
                                         output, 4) == 0);
    memcpy(input, "XX", 2);
    value = 1; /* Out-of-order: the next required offset is zero. */
    memcpy(input + 0x1000, &value, 4);
    value = 2;
    memcpy(input + 0x1004, &value, 4);
    memset(output, 0xff, sizeof(output));
    assert(dcp_iomfb_properties_callback(&properties, 127, input, 0x1008,
                                         output, 4) != 0);
    assert(load_u32(output) == 0);

    dcp_iomfb_properties_destroy(&properties);
    assert(fixture.allocations == fixture.releases);
    puts("dcp_iomfb_properties_test: ok");
    return 0;
}
