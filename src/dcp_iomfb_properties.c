/* SPDX-License-Identifier: MIT */

#include "dcp_iomfb_properties.h"

#ifdef DCP_IOMFB_PROPERTIES_HOST_TEST
#include <string.h>
#else
#include "string.h"
#endif

static uint32_t load_u32(const void *pointer)
{
    uint32_t value;
    memcpy(&value, pointer, sizeof(value));
    return value;
}

static void store_result(void *output, uint32_t output_size, bool success)
{
    uint32_t result = success ? 1 : 0;
    if (output && output_size == sizeof(result))
        memcpy(output, &result, sizeof(result));
}

static void abort_incoming(struct dcp_iomfb_properties *properties)
{
    if (properties->incoming && properties->ops && properties->ops->release)
        properties->ops->release(properties->opaque, properties->incoming);
    properties->incoming = NULL;
    properties->incoming_size = 0;
    properties->incoming_offset = 0;
}

void dcp_iomfb_properties_init(struct dcp_iomfb_properties *properties,
                               const struct dcp_iomfb_property_ops *ops,
                               void *opaque)
{
    if (!properties)
        return;
    memset(properties, 0, sizeof(*properties));
    properties->ops = ops;
    properties->opaque = opaque;
}

void dcp_iomfb_properties_destroy(struct dcp_iomfb_properties *properties)
{
    unsigned int i;

    if (!properties)
        return;
    abort_incoming(properties);
    if (properties->ops && properties->ops->release) {
        for (i = 0; i < DCP_IOMFB_PROPERTY_MAX_RECORDS; i++) {
            if (properties->records[i].data)
                properties->ops->release(properties->opaque,
                                          properties->records[i].data);
        }
    }
    memset(properties, 0, sizeof(*properties));
}

static int start_transfer(struct dcp_iomfb_properties *properties,
                          const void *input, void *output,
                          uint32_t output_size)
{
    size_t wire_size = load_u32(input);
    size_t size;

    if (wire_size <= 1 || wire_size - 1 > DCP_IOMFB_PROPERTY_MAX_SIZE ||
        properties->incoming ||
        !properties->ops || !properties->ops->allocate) {
        store_result(output, output_size, false);
        return -1;
    }
    size = wire_size - 1;
    properties->incoming = properties->ops->allocate(properties->opaque, size);
    if (!properties->incoming) {
        store_result(output, output_size, false);
        return -1;
    }
    memset(properties->incoming, 0, size);
    properties->incoming_size = size;
    properties->incoming_offset = 0;
    store_result(output, output_size, true);
    return 0;
}

static int append_chunk(struct dcp_iomfb_properties *properties,
                        const void *input, void *output,
                        uint32_t output_size)
{
    const uint8_t *bytes = input;
    size_t offset = load_u32(bytes + DCP_IOMFB_PROPERTY_CHUNK_SIZE);
    size_t length = load_u32(bytes + DCP_IOMFB_PROPERTY_CHUNK_SIZE + 4);

    if (!properties->incoming || !length ||
        length > DCP_IOMFB_PROPERTY_CHUNK_SIZE ||
        offset != properties->incoming_offset ||
        offset > properties->incoming_size ||
        length > properties->incoming_size - offset) {
        store_result(output, output_size, false);
        abort_incoming(properties);
        return -1;
    }
    memcpy((uint8_t *)properties->incoming + offset, bytes, length);
    properties->incoming_offset += length;
    store_result(output, output_size, true);
    return 0;
}

static int finish_transfer(struct dcp_iomfb_properties *properties,
                           const void *input, void *output,
                           uint32_t output_size)
{
    const char *key = input;
    struct dcp_iomfb_property_record *record = NULL;
    unsigned int i;
    size_t key_length = 0;

    while (key_length < DCP_IOMFB_PROPERTY_KEY_SIZE && key[key_length])
        key_length++;
    if (!properties->incoming ||
        properties->incoming_offset != properties->incoming_size ||
        !key_length || key_length == DCP_IOMFB_PROPERTY_KEY_SIZE) {
        store_result(output, output_size, false);
        abort_incoming(properties);
        return -1;
    }
    for (i = 0; i < DCP_IOMFB_PROPERTY_MAX_RECORDS; i++) {
        if (properties->records[i].data &&
            strcmp(properties->records[i].key, key) == 0) {
            record = &properties->records[i];
            break;
        }
        if (!record && !properties->records[i].data)
            record = &properties->records[i];
    }
    if (!record) {
        store_result(output, output_size, false);
        abort_incoming(properties);
        return -1;
    }
    if (record->data)
        properties->ops->release(properties->opaque, record->data);
    memset(record, 0, sizeof(*record));
    memcpy(record->key, key, key_length);
    record->data = properties->incoming;
    record->size = properties->incoming_size;
    properties->incoming = NULL;
    properties->incoming_size = 0;
    properties->incoming_offset = 0;
    store_result(output, output_size, true);
    return 0;
}

int dcp_iomfb_properties_callback(struct dcp_iomfb_properties *properties,
                                  unsigned int callback_id,
                                  const void *input, uint32_t input_size,
                                  void *output, uint32_t output_size)
{
    if (!properties || !input || !output || output_size != 4)
        return -1;
    switch (callback_id) {
    case 126:
        if (input_size != 4)
            return -1;
        return start_transfer(properties, input, output, output_size);
    case 127:
        if (input_size != DCP_IOMFB_PROPERTY_CHUNK_SIZE + 8)
            return -1;
        return append_chunk(properties, input, output, output_size);
    case 128:
        if (input_size != DCP_IOMFB_PROPERTY_KEY_SIZE)
            return -1;
        return finish_transfer(properties, input, output, output_size);
    default:
        return -1;
    }
}

bool dcp_iomfb_properties_find(const struct dcp_iomfb_properties *properties,
                               const char *key, const void **data,
                               size_t *size)
{
    unsigned int i;

    if (!properties || !key || !data || !size)
        return false;
    for (i = 0; i < DCP_IOMFB_PROPERTY_MAX_RECORDS; i++) {
        if (properties->records[i].data &&
            strcmp(properties->records[i].key, key) == 0) {
            *data = properties->records[i].data;
            *size = properties->records[i].size;
            return true;
        }
    }
    return false;
}
