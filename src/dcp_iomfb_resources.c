/* SPDX-License-Identifier: MIT */

#include "dcp_iomfb_resources.h"

#ifdef DCP_IOMFB_RESOURCES_HOST_TEST
#include <string.h>
#else
#include "string.h"
#endif

#define DCP_PAGE_SIZE  0x1000ull
#define DCP_DART_SIZE  0x4000ull

static dcp_iomfb_res_u32 load_u32(const void *pointer)
{
    dcp_iomfb_res_u32 value;
    memcpy(&value, pointer, sizeof(value));
    return value;
}

static dcp_iomfb_res_u64 load_u64(const void *pointer)
{
    dcp_iomfb_res_u64 value;
    memcpy(&value, pointer, sizeof(value));
    return value;
}

static void store_u32(void *pointer, dcp_iomfb_res_u32 value)
{
    memcpy(pointer, &value, sizeof(value));
}

static void store_u64(void *pointer, dcp_iomfb_res_u64 value)
{
    memcpy(pointer, &value, sizeof(value));
}

static bool align_up(dcp_iomfb_res_u64 value, dcp_iomfb_res_u64 alignment,
                     dcp_iomfb_res_u64 *result)
{
    dcp_iomfb_res_u64 mask = alignment - 1;
    if (!value || value > ~(dcp_iomfb_res_u64)0 - mask)
        return false;
    *result = (value + mask) & ~mask;
    return true;
}

static struct dcp_iomfb_descriptor *find_descriptor(
    struct dcp_iomfb_resources *resources, dcp_iomfb_res_u64 id)
{
    if (!resources || id == 0 || id >= DCP_IOMFB_MAX_DESCRIPTORS ||
        !resources->descriptors[id].valid)
        return NULL;
    return &resources->descriptors[id];
}

static unsigned int allocate_descriptor_id(struct dcp_iomfb_resources *resources)
{
    unsigned int id;
    for (id = 1; id < DCP_IOMFB_MAX_DESCRIPTORS; id++)
        if (!resources->descriptors[id].valid)
            return id;
    return 0;
}

static void release_descriptor(struct dcp_iomfb_resources *resources,
                               struct dcp_iomfb_descriptor *descriptor)
{
    if (descriptor->piodma_mapped && resources->ops->unmap_piodma)
        resources->ops->unmap_piodma(resources->opaque, descriptor->dva,
                                     descriptor->map_size);
    if (descriptor->dva && resources->ops->unmap_dcp)
        resources->ops->unmap_dcp(resources->opaque, descriptor->dva,
                                  descriptor->map_size);
    if (descriptor->allocated && descriptor->cpu && resources->ops->release)
        resources->ops->release(resources->opaque, descriptor->cpu,
                                descriptor->map_size);
    memset(descriptor, 0, sizeof(*descriptor));
}

void dcp_iomfb_resources_init(struct dcp_iomfb_resources *resources,
                              const struct dcp_iomfb_resource_ops *ops,
                              void *opaque)
{
    memset(resources, 0, sizeof(*resources));
    resources->ops = ops;
    resources->opaque = opaque;
}

void dcp_iomfb_resources_destroy(struct dcp_iomfb_resources *resources)
{
    unsigned int i;
    if (!resources || !resources->ops)
        return;
    for (i = 1; i < DCP_IOMFB_MAX_DESCRIPTORS; i++)
        if (resources->descriptors[i].valid)
            release_descriptor(resources, &resources->descriptors[i]);
    for (i = 0; i < DCP_IOMFB_MAX_REG_MAPPINGS; i++) {
        if (resources->regs[i].valid && resources->ops->unmap_dcp)
            resources->ops->unmap_dcp(resources->opaque,
                                      resources->regs[i].dva,
                                      resources->regs[i].size);
    }
    memset(resources, 0, sizeof(*resources));
}

static int callback_rt_bandwidth(struct dcp_iomfb_resources *resources,
                                 void *output)
{
    dcp_iomfb_res_u64 scratch_pa, scratch_size, doorbell_pa, doorbell_size;
    if (!resources->ops->get_reg ||
        !resources->ops->get_reg(resources->opaque, 5, &scratch_pa,
                                 &scratch_size) || scratch_size < 0x18 ||
        !resources->ops->get_reg(resources->opaque, 6, &doorbell_pa,
                                 &doorbell_size) || doorbell_size < 4)
        return -1;
    store_u64((unsigned char *)output + 8, scratch_pa + 0x14);
    store_u64((unsigned char *)output + 16, doorbell_pa);
    store_u32((unsigned char *)output + 28, 2);
    store_u32((unsigned char *)output + 44, 4);
    return 0;
}

static int callback_map_reg(struct dcp_iomfb_resources *resources,
                            const void *input, void *output)
{
    const unsigned char *bytes = input;
    unsigned int index = load_u32(bytes + 4);
    dcp_iomfb_res_u64 pa, size, dva;
    unsigned int i;

    if (memcmp(bytes, "PROV", 4) != 0 || !resources->ops->get_reg ||
        !resources->ops->map_dcp ||
        !resources->ops->get_reg(resources->opaque, index, &pa, &size) ||
        !pa || !size)
        goto rejected;
    for (i = 0; i < DCP_IOMFB_MAX_REG_MAPPINGS; i++) {
        if (resources->regs[i].valid && resources->regs[i].index == index) {
            dva = resources->regs[i].dva;
            goto mapped;
        }
    }
    for (i = 0; i < DCP_IOMFB_MAX_REG_MAPPINGS; i++)
        if (!resources->regs[i].valid)
            break;
    if (i == DCP_IOMFB_MAX_REG_MAPPINGS ||
        !resources->ops->map_dcp(resources->opaque, pa, size, &dva))
        goto rejected;
    resources->regs[i].valid = true;
    resources->regs[i].index = index;
    resources->regs[i].dva = dva;
    resources->regs[i].size = size;
mapped:
    store_u64((unsigned char *)output + 0, dva);
    store_u64((unsigned char *)output + 8, pa);
    store_u64((unsigned char *)output + 16, size);
    store_u32((unsigned char *)output + 24, 0);
    return 0;
rejected:
    store_u32((unsigned char *)output + 24, 1);
    return 0;
}

static int callback_allocate(struct dcp_iomfb_resources *resources,
                             const void *input, void *output)
{
    dcp_iomfb_res_u64 requested = load_u64((const unsigned char *)input + 4);
    dcp_iomfb_res_u64 protocol_size, map_size, pa = 0, dva = 0;
    void *cpu = NULL;
    unsigned int id = allocate_descriptor_id(resources);
    struct dcp_iomfb_descriptor *descriptor;

    if (!id || !resources->ops->allocate || !resources->ops->map_dcp ||
        !align_up(requested, DCP_PAGE_SIZE, &protocol_size) ||
        !align_up(protocol_size, DCP_DART_SIZE, &map_size) ||
        !resources->ops->allocate(resources->opaque, map_size, DCP_DART_SIZE,
                                  &pa, &cpu) || !pa || !cpu)
        return 0;
    memset(cpu, 0, (size_t)map_size);
    if (!resources->ops->map_dcp(resources->opaque, pa, map_size, &dva) ||
        !dva) {
        resources->ops->release(resources->opaque, cpu, map_size);
        return 0;
    }
    descriptor = &resources->descriptors[id];
    descriptor->valid = true;
    descriptor->allocated = true;
    descriptor->cpu = cpu;
    descriptor->pa = pa;
    descriptor->dva = dva;
    descriptor->protocol_size = protocol_size;
    descriptor->map_size = map_size;
    store_u64((unsigned char *)output + 0, pa);
    store_u64((unsigned char *)output + 8, dva);
    store_u64((unsigned char *)output + 16, protocol_size);
    store_u32((unsigned char *)output + 24, id);
    return 0;
}

static bool physical_range_allowed(struct dcp_iomfb_resources *resources,
                                   dcp_iomfb_res_u64 pa,
                                   dcp_iomfb_res_u64 size)
{
    unsigned int index;
    dcp_iomfb_res_u64 reg_pa, reg_size, end, reg_end;
    if (!size || pa > ~(dcp_iomfb_res_u64)0 - size)
        return false;
    end = pa + size;
    for (index = 0; index < DCP_IOMFB_MAX_REG_MAPPINGS; index++) {
        if (!resources->ops->get_reg(resources->opaque, index, &reg_pa,
                                     &reg_size))
            continue;
        if (reg_pa > ~(dcp_iomfb_res_u64)0 - reg_size)
            continue;
        reg_end = reg_pa + reg_size;
        if (pa >= reg_pa && end <= reg_end)
            return true;
    }
    return false;
}

static int callback_map_physical(struct dcp_iomfb_resources *resources,
                                 const void *input, void *output)
{
    dcp_iomfb_res_u64 pa = load_u64(input);
    dcp_iomfb_res_u64 requested = load_u64((const unsigned char *)input + 8);
    dcp_iomfb_res_u64 protocol_size, map_size, dva;
    unsigned int id = allocate_descriptor_id(resources);
    struct dcp_iomfb_descriptor *descriptor;
    if (!id || !resources->ops->map_dcp ||
        !align_up(requested, DCP_PAGE_SIZE, &protocol_size) ||
        !align_up(protocol_size, DCP_DART_SIZE, &map_size) ||
        !physical_range_allowed(resources, pa, map_size) ||
        !resources->ops->map_dcp(resources->opaque, pa, map_size, &dva))
        return 0;
    descriptor = &resources->descriptors[id];
    descriptor->valid = true;
    descriptor->pa = pa;
    descriptor->dva = dva;
    descriptor->protocol_size = protocol_size;
    descriptor->map_size = map_size;
    store_u64((unsigned char *)output + 0, dva);
    store_u64((unsigned char *)output + 8, protocol_size);
    store_u32((unsigned char *)output + 16, id);
    return 0;
}

static int callback_map_piodma(struct dcp_iomfb_resources *resources,
                               const void *input, void *output)
{
    dcp_iomfb_res_u64 id = load_u64(input);
    struct dcp_iomfb_descriptor *descriptor = find_descriptor(resources, id);
    if (!descriptor || !descriptor->allocated || descriptor->piodma_mapped ||
        !resources->ops->map_piodma ||
        !resources->ops->map_piodma(resources->opaque, descriptor->pa,
                                    descriptor->dva, descriptor->map_size)) {
        store_u32((unsigned char *)output + 16, 22);
        return 0;
    }
    descriptor->piodma_mapped = true;
    store_u64((unsigned char *)output + 8, descriptor->dva);
    store_u32((unsigned char *)output + 16, 0);
    return 0;
}

static int callback_unmap_piodma(struct dcp_iomfb_resources *resources,
                                 const void *input)
{
    dcp_iomfb_res_u64 id = load_u64(input);
    struct dcp_iomfb_descriptor *descriptor = find_descriptor(resources, id);
    if (!descriptor || !descriptor->piodma_mapped)
        return 0;
    if (resources->ops->unmap_piodma)
        resources->ops->unmap_piodma(resources->opaque, descriptor->dva,
                                     descriptor->map_size);
    descriptor->piodma_mapped = false;
    return 0;
}

int dcp_iomfb_resources_callback(struct dcp_iomfb_resources *resources,
                                 unsigned int callback_id, const void *input,
                                 dcp_iomfb_res_u32 input_size, void *output,
                                 dcp_iomfb_res_u32 output_size)
{
    struct dcp_iomfb_descriptor *descriptor;
    dcp_iomfb_res_u64 id;
    if (!resources || !resources->ops || (input_size && !input) ||
        (output_size && !output))
        return -1;
    if (output_size)
        memset(output, 0, output_size);
    switch (callback_id) {
    case 3:
        return input_size == 4 && output_size == 0x3c ?
                   callback_rt_bandwidth(resources, output) : -1;
    case 201:
        return input_size == 0xc && output_size == 0x14 ?
                   callback_map_piodma(resources, input, output) : -1;
    case 202:
        return input_size == 0x1c && output_size == 0 ?
                   callback_unmap_piodma(resources, input) : -1;
    case 408:
        if (input_size != 8 || output_size != 8 || !resources->ops->get_clock)
            return -1;
        store_u64(output, resources->ops->get_clock(resources->opaque));
        return 0;
    case 411:
        return input_size == 0x10 && output_size == 0x1c ?
                   callback_map_reg(resources, input, output) : -1;
    case 451:
        return input_size == 0x14 && output_size == 0x1c ?
                   callback_allocate(resources, input, output) : -1;
    case 452:
        return input_size == 0x18 && output_size == 0x14 ?
                   callback_map_physical(resources, input, output) : -1;
    case 454:
    case 455:
        if (input_size != 8 || output_size != 4)
            return -1;
        descriptor = find_descriptor(resources, load_u32(input));
        if (!descriptor)
            return 0;
        if (callback_id == 454 &&
            descriptor->prepare_count == (unsigned int)-1)
            return -1;
        if (callback_id == 454)
            descriptor->prepare_count++;
        else if (descriptor->prepare_count)
            descriptor->prepare_count--;
        else
            return 0;
        store_u32(output, 0);
        return 0;
    case 456:
        if (input_size != 4 || output_size != 4)
            return -1;
        id = load_u32(input);
        descriptor = find_descriptor(resources, id);
        if (!descriptor || descriptor->prepare_count)
            return 0;
        release_descriptor(resources, descriptor);
        store_u32(output, 1);
        return 0;
    default:
        return -1;
    }
}
