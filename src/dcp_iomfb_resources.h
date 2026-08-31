/* SPDX-License-Identifier: MIT */

#ifndef DCP_IOMFB_RESOURCES_H
#define DCP_IOMFB_RESOURCES_H

#ifdef DCP_IOMFB_RESOURCES_HOST_TEST
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t dcp_iomfb_res_u32;
typedef uint64_t dcp_iomfb_res_u64;
#else
#include "types.h"
typedef u32 dcp_iomfb_res_u32;
typedef u64 dcp_iomfb_res_u64;
#endif

#define DCP_IOMFB_MAX_DESCRIPTORS 128u
#define DCP_IOMFB_MAX_REG_MAPPINGS 16u

struct dcp_iomfb_resource_ops {
    bool (*allocate)(void *opaque, dcp_iomfb_res_u64 size,
                     dcp_iomfb_res_u64 alignment, dcp_iomfb_res_u64 *pa,
                     void **cpu);
    void (*release)(void *opaque, void *cpu, dcp_iomfb_res_u64 size);
    bool (*map_dcp)(void *opaque, dcp_iomfb_res_u64 pa,
                    dcp_iomfb_res_u64 size, dcp_iomfb_res_u64 *dva);
    void (*unmap_dcp)(void *opaque, dcp_iomfb_res_u64 dva,
                      dcp_iomfb_res_u64 size);
    bool (*map_piodma)(void *opaque, dcp_iomfb_res_u64 pa,
                       dcp_iomfb_res_u64 dva, dcp_iomfb_res_u64 size);
    void (*unmap_piodma)(void *opaque, dcp_iomfb_res_u64 dva,
                         dcp_iomfb_res_u64 size);
    bool (*get_reg)(void *opaque, unsigned int index,
                    dcp_iomfb_res_u64 *pa, dcp_iomfb_res_u64 *size);
    dcp_iomfb_res_u64 (*get_clock)(void *opaque);
};

struct dcp_iomfb_descriptor {
    bool valid;
    bool allocated;
    bool piodma_mapped;
    unsigned int prepare_count;
    void *cpu;
    dcp_iomfb_res_u64 pa;
    dcp_iomfb_res_u64 dva;
    dcp_iomfb_res_u64 protocol_size;
    dcp_iomfb_res_u64 map_size;
};

struct dcp_iomfb_reg_mapping {
    bool valid;
    unsigned int index;
    dcp_iomfb_res_u64 dva;
    dcp_iomfb_res_u64 size;
};

struct dcp_iomfb_resources {
    const struct dcp_iomfb_resource_ops *ops;
    void *opaque;
    struct dcp_iomfb_descriptor descriptors[DCP_IOMFB_MAX_DESCRIPTORS];
    struct dcp_iomfb_reg_mapping regs[DCP_IOMFB_MAX_REG_MAPPINGS];
};

void dcp_iomfb_resources_init(struct dcp_iomfb_resources *resources,
                              const struct dcp_iomfb_resource_ops *ops,
                              void *opaque);
void dcp_iomfb_resources_destroy(struct dcp_iomfb_resources *resources);
int dcp_iomfb_resources_callback(struct dcp_iomfb_resources *resources,
                                 unsigned int callback_id, const void *input,
                                 dcp_iomfb_res_u32 input_size, void *output,
                                 dcp_iomfb_res_u32 output_size);

#endif
