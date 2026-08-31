/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/dcp_iomfb_resources.h"

struct fixture {
    unsigned allocs;
    unsigned frees;
    unsigned dcp_maps;
    unsigned dcp_unmaps;
    unsigned piodma_maps;
    unsigned piodma_unmaps;
    uint64_t last_dva;
};

static bool allocate(void *opaque, uint64_t size, uint64_t alignment,
                     uint64_t *pa, void **cpu)
{
    struct fixture *fixture = opaque;
    static uint8_t memory[0x10000];

    assert(size == 0x4000 && alignment == 0x4000);
    fixture->allocs++;
    *pa = 0x80000000;
    *cpu = memory;
    return true;
}

static void release(void *opaque, void *cpu, uint64_t size)
{
    struct fixture *fixture = opaque;
    assert(cpu != NULL && size == 0x4000);
    fixture->frees++;
}

static bool map_dcp(void *opaque, uint64_t pa, uint64_t size, uint64_t *dva)
{
    struct fixture *fixture = opaque;
    assert(pa != 0 && size != 0);
    fixture->dcp_maps++;
    *dva = 0x90000000 + fixture->dcp_maps * 0x10000;
    fixture->last_dva = *dva;
    return true;
}

static void unmap_dcp(void *opaque, uint64_t dva, uint64_t size)
{
    struct fixture *fixture = opaque;
    assert(dva != 0 && size != 0);
    fixture->dcp_unmaps++;
}

static bool map_piodma(void *opaque, uint64_t pa, uint64_t dva,
                       uint64_t size)
{
    struct fixture *fixture = opaque;
    assert(pa == 0x80000000 && dva == fixture->last_dva && size == 0x4000);
    fixture->piodma_maps++;
    return true;
}

static void unmap_piodma(void *opaque, uint64_t dva, uint64_t size)
{
    struct fixture *fixture = opaque;
    assert(dva == fixture->last_dva && size == 0x4000);
    fixture->piodma_unmaps++;
}

static bool get_reg(void *opaque, unsigned int index, uint64_t *pa,
                    uint64_t *size)
{
    (void)opaque;
    if (index >= 7)
        return false;
    *pa = 0x23b000000 + index * 0x100000;
    *size = 0x10000;
    return true;
}

static uint64_t get_clock(void *opaque)
{
    (void)opaque;
    return 533333328;
}

static uint32_t load_u32(const uint8_t *p)
{
    uint32_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static uint64_t load_u64(const uint8_t *p)
{
    uint64_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static void store_u32(uint8_t *p, uint32_t value)
{
    memcpy(p, &value, sizeof(value));
}

static void store_u64(uint8_t *p, uint64_t value)
{
    memcpy(p, &value, sizeof(value));
}

int main(void)
{
    struct fixture fixture = {0};
    const struct dcp_iomfb_resource_ops ops = {
        .allocate = allocate,
        .release = release,
        .map_dcp = map_dcp,
        .unmap_dcp = unmap_dcp,
        .map_piodma = map_piodma,
        .unmap_piodma = unmap_piodma,
        .get_reg = get_reg,
        .get_clock = get_clock,
    };
    struct dcp_iomfb_resources resources;
    uint8_t input[0x20] = {0};
    uint8_t output[0x40] = {0};
    uint32_t id;
    uint64_t descriptor_dva;

    dcp_iomfb_resources_init(&resources, &ops, &fixture);

    assert(dcp_iomfb_resources_callback(&resources, 3, input, 4,
                                         output, 0x3c) == 0);
    assert(load_u64(output + 8) == 0x23b000000 + 5 * 0x100000 + 0x14);
    assert(load_u64(output + 16) == 0x23b000000 + 6 * 0x100000);
    assert(load_u32(output + 28) == 2);
    assert(load_u32(output + 44) == 4);

    memset(output, 0, sizeof(output));
    assert(dcp_iomfb_resources_callback(&resources, 408, input, 8,
                                         output, 8) == 0);
    assert(load_u64(output) == 533333328);

    memset(input, 0, sizeof(input));
    memcpy(input, "PROV", 4);
    store_u32(input + 4, 4);
    memset(output, 0, sizeof(output));
    assert(dcp_iomfb_resources_callback(&resources, 411, input, 0x10,
                                         output, 0x1c) == 0);
    assert(load_u64(output + 8) == 0x23b000000 + 4 * 0x100000);
    assert(load_u64(output + 16) == 0x10000);
    assert(load_u32(output + 24) == 0);

    memset(input, 0, sizeof(input));
    store_u64(input + 4, 0x3001);
    memset(output, 0, sizeof(output));
    assert(dcp_iomfb_resources_callback(&resources, 451, input, 0x14,
                                         output, 0x1c) == 0);
    assert(load_u64(output) == 0x80000000);
    descriptor_dva = load_u64(output + 8);
    assert(descriptor_dva == fixture.last_dva);
    assert(load_u64(output + 16) == 0x4000);
    id = load_u32(output + 24);
    assert(id != 0);

    memset(input, 0, sizeof(input));
    store_u32(input, id);
    resources.descriptors[id].prepare_count = UINT32_MAX;
    memset(output, 0xff, 4);
    assert(dcp_iomfb_resources_callback(&resources, 454, input, 8,
                                         output, 4) != 0);
    assert(resources.descriptors[id].prepare_count == UINT32_MAX);
    assert(load_u32(output) == 0);
    resources.descriptors[id].prepare_count = 0;

    memset(input, 0, sizeof(input));
    store_u64(input, id);
    memset(output, 0, sizeof(output));
    assert(dcp_iomfb_resources_callback(&resources, 201, input, 0xc,
                                         output, 0x14) == 0);
    assert(load_u64(output + 8) == descriptor_dva);
    assert(load_u32(output + 16) == 0);

    memset(input, 0, sizeof(input));
    store_u64(input, id);
    store_u64(input + 12, descriptor_dva);
    assert(dcp_iomfb_resources_callback(&resources, 202, input, 0x1c,
                                         NULL, 0) == 0);

    memset(input, 0, sizeof(input));
    store_u32(input, id);
    store_u32(input + 4, 0);
    memset(output, 0, sizeof(output));
    assert(dcp_iomfb_resources_callback(&resources, 454, input, 8,
                                         output, 4) == 0);
    assert(load_u32(output) == 0);
    assert(dcp_iomfb_resources_callback(&resources, 455, input, 8,
                                         output, 4) == 0);
    assert(load_u32(output) == 0);

    assert(dcp_iomfb_resources_callback(&resources, 456, input, 4,
                                         output, 4) == 0);
    assert(load_u32(output) == 1);
    assert(fixture.allocs == 1 && fixture.frees == 1);
    assert(fixture.dcp_maps == 2 && fixture.dcp_unmaps == 1);
    assert(fixture.piodma_maps == 1 && fixture.piodma_unmaps == 1);

    memset(output, 0xff, 4);
    assert(dcp_iomfb_resources_callback(&resources, 456, input, 4,
                                         output, 4) == 0);
    assert(load_u32(output) == 0);

    dcp_iomfb_resources_destroy(&resources);
    assert(fixture.dcp_unmaps == 2);
    puts("dcp_iomfb_resources_test: ok");
    return 0;
}
