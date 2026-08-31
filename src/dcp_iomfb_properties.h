/* SPDX-License-Identifier: MIT */

#ifndef DCP_IOMFB_PROPERTIES_H
#define DCP_IOMFB_PROPERTIES_H

#ifdef DCP_IOMFB_PROPERTIES_HOST_TEST
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#else
#include "types.h"
#endif

#define DCP_IOMFB_PROPERTY_KEY_SIZE     0x40u
#define DCP_IOMFB_PROPERTY_CHUNK_SIZE   0x1000u
#define DCP_IOMFB_PROPERTY_MAX_SIZE     0x100000u
#define DCP_IOMFB_PROPERTY_MAX_RECORDS  8u

struct dcp_iomfb_property_ops {
    void *(*allocate)(void *opaque, size_t size);
    void (*release)(void *opaque, void *pointer);
};

struct dcp_iomfb_property_record {
    char key[DCP_IOMFB_PROPERTY_KEY_SIZE];
    void *data;
    size_t size;
};

struct dcp_iomfb_properties {
    const struct dcp_iomfb_property_ops *ops;
    void *opaque;
    void *incoming;
    size_t incoming_size;
    size_t incoming_offset;
    struct dcp_iomfb_property_record records[DCP_IOMFB_PROPERTY_MAX_RECORDS];
};

void dcp_iomfb_properties_init(struct dcp_iomfb_properties *properties,
                               const struct dcp_iomfb_property_ops *ops,
                               void *opaque);
void dcp_iomfb_properties_destroy(struct dcp_iomfb_properties *properties);
int dcp_iomfb_properties_callback(struct dcp_iomfb_properties *properties,
                                  unsigned int callback_id,
                                  const void *input, uint32_t input_size,
                                  void *output, uint32_t output_size);
bool dcp_iomfb_properties_find(const struct dcp_iomfb_properties *properties,
                               const char *key, const void **data,
                               size_t *size);

#endif
