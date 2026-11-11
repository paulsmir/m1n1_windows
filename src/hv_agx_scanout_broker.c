/* SPDX-License-Identifier: MIT */

#include "hv_agx_scanout_broker.h"

#include <string.h>

#define SCANOUT_V1_CAPS                                                         \
    (HV_AGX_SCANOUT_CAP_FIXED_J313 | HV_AGX_SCANOUT_CAP_BGRA8888 |             \
     HV_AGX_SCANOUT_CAP_REGISTERED_POOL | HV_AGX_SCANOUT_CAP_APPLIED_RECEIPT | \
     HV_AGX_SCANOUT_CAP_RGBA8888)

static bool add_overflows(uint64_t left, uint64_t right)
{
    return left > UINT64_MAX - right;
}

static bool aligned(uint64_t value)
{
    return value != 0 && (value & (HV_AGX_SCANOUT_ALIGNMENT - 1)) == 0;
}

static void reject(struct hv_agx_scanout_broker *broker,
                   enum hv_agx_scanout_result result)
{
    broker->result = result;
    broker->rejected_requests++;
}

static void reject_terminal(struct hv_agx_scanout_broker *broker,
                            enum hv_agx_scanout_result result, uint64_t sequence)
{
    broker->highest_sequence = sequence;
    broker->receipt_sequence = sequence;
    reject(broker, result);
}

static bool sequence_fresh(const struct hv_agx_scanout_broker *broker, uint64_t sequence)
{
    return sequence != 0 && sequence > broker->highest_sequence;
}

static bool format_valid(const struct hv_agx_scanout_broker *broker)
{
    return broker->staging_format == HV_AGX_SCANOUT_FORMAT_BGRA8888 ||
           (broker->staging_format == HV_AGX_SCANOUT_FORMAT_RGBA8888 &&
            (broker->capabilities & HV_AGX_SCANOUT_CAP_RGBA8888));
}

static bool scaled_source(const struct hv_agx_scanout_broker *broker)
{
    return (broker->capabilities & HV_AGX_SCANOUT_CAP_SCALED_SOURCE) != 0;
}

/* rc106: with CAP_SCALED_SOURCE the source may be any linear surface up to
 * the panel (stride a multiple of 64 bytes, DCP's rule) and the destination
 * any non-empty rectangle inside the panel. */
static bool scaled_geometry_valid(const struct hv_agx_scanout_broker *broker)
{
    uint32_t width = broker->staging_width;
    uint32_t height = broker->staging_height;
    uint32_t stride = broker->staging_stride;
    uint32_t dest_x = HV_AGX_SCANOUT_DEST_LOW(broker->staging_dest_origin);
    uint32_t dest_y = HV_AGX_SCANOUT_DEST_HIGH(broker->staging_dest_origin);
    uint32_t dest_w = HV_AGX_SCANOUT_DEST_LOW(broker->staging_dest_size);
    uint32_t dest_h = HV_AGX_SCANOUT_DEST_HIGH(broker->staging_dest_size);

    if (width == 0 || width > HV_AGX_SCANOUT_J313_WIDTH || height == 0 ||
        height > HV_AGX_SCANOUT_J313_HEIGHT ||
        (uint64_t)stride < (uint64_t)width * 4 || (stride & 63) != 0 ||
        broker->staging_surface_size != (uint64_t)stride * height)
        return false;
    return dest_w != 0 && dest_h != 0 &&
           dest_w <= HV_AGX_SCANOUT_J313_WIDTH &&
           dest_x <= HV_AGX_SCANOUT_J313_WIDTH - dest_w &&
           dest_h <= HV_AGX_SCANOUT_J313_HEIGHT &&
           dest_y <= HV_AGX_SCANOUT_J313_HEIGHT - dest_h;
}

static bool native_geometry_valid(const struct hv_agx_scanout_broker *broker)
{
    return broker->staging_surface_size == HV_AGX_SCANOUT_J313_SURFACE_SIZE &&
           broker->staging_width == HV_AGX_SCANOUT_J313_WIDTH &&
           broker->staging_height == HV_AGX_SCANOUT_J313_HEIGHT &&
           broker->staging_stride == HV_AGX_SCANOUT_J313_STRIDE;
}

static bool surface_valid(const struct hv_agx_scanout_broker *broker)
{
    uint64_t offset = broker->staging_surface_offset;
    uint64_t size = broker->staging_surface_size;

    if (broker->registered_pool_size != broker->pool_size ||
        (offset & (HV_AGX_SCANOUT_ALIGNMENT - 1)) != 0 || !format_valid(broker) ||
        !(scaled_source(broker) ? scaled_geometry_valid(broker)
                                : native_geometry_valid(broker)) ||
        add_overflows(offset, size) ||
        offset + size > broker->registered_pool_size)
        return false;
    return true;
}

static bool two_layers_advertised(const struct hv_agx_scanout_broker *broker)
{
    return (broker->capabilities & HV_AGX_SCANOUT_CAP_TWO_LAYERS) != 0;
}

static bool scale_ok(uint32_t source, uint32_t destination)
{
    return (uint64_t)destination <= (uint64_t)HV_AGX_SCANOUT_V3_MAX_SCALE * source &&
           (uint64_t)source <= (uint64_t)HV_AGX_SCANOUT_V3_MAX_SCALE * destination;
}

/* V3: one raw record -> validated layer, or the refusal code. */
static enum hv_agx_scanout_result
layer_validate(const struct hv_agx_scanout_broker *broker,
               const struct hv_agx_scanout_layer_staging *raw,
               struct hv_agx_scanout_layer *layer)
{
    uint32_t src_x = HV_AGX_SCANOUT_DEST_LOW(raw->src_origin);
    uint32_t src_y = HV_AGX_SCANOUT_DEST_HIGH(raw->src_origin);
    uint32_t src_w = HV_AGX_SCANOUT_DEST_LOW(raw->src_size);
    uint32_t src_h = HV_AGX_SCANOUT_DEST_HIGH(raw->src_size);
    uint32_t dst_x = HV_AGX_SCANOUT_DEST_LOW(raw->dst_origin);
    uint32_t dst_y = HV_AGX_SCANOUT_DEST_HIGH(raw->dst_origin);
    uint32_t dst_w = HV_AGX_SCANOUT_DEST_LOW(raw->dst_size);
    uint32_t dst_h = HV_AGX_SCANOUT_DEST_HIGH(raw->dst_size);

    if (raw->reserved0 || raw->reserved1 ||
        (raw->flags & ~HV_AGX_SCANOUT_LAYER_FLAGS_VALID))
        return HV_AGX_SCANOUT_RESULT_LAYER_RESERVED;
    if (raw->format != HV_AGX_SCANOUT_FORMAT_BGRA8888 &&
        raw->format != HV_AGX_SCANOUT_FORMAT_RGBA8888)
        return HV_AGX_SCANOUT_RESULT_LAYER_FORMAT;
    if (!raw->width || raw->width > HV_AGX_SCANOUT_J313_WIDTH || !raw->height ||
        raw->height > HV_AGX_SCANOUT_J313_HEIGHT)
        return HV_AGX_SCANOUT_RESULT_LAYER_GEOMETRY;
    if ((uint64_t)raw->stride < (uint64_t)raw->width * 4 || (raw->stride & 63))
        return HV_AGX_SCANOUT_RESULT_LAYER_STRIDE;
    if (raw->surface_size != (uint64_t)raw->stride * raw->height)
        return HV_AGX_SCANOUT_RESULT_LAYER_GEOMETRY;
    if ((raw->surface_offset & (HV_AGX_SCANOUT_ALIGNMENT - 1)) != 0 ||
        broker->registered_pool_size != broker->pool_size ||
        add_overflows(raw->surface_offset, raw->surface_size) ||
        raw->surface_offset + raw->surface_size > broker->registered_pool_size)
        return HV_AGX_SCANOUT_RESULT_LAYER_OUTSIDE_POOL;
    if (!src_w || !src_h || src_w > raw->width || src_x > raw->width - src_w ||
        src_h > raw->height || src_y > raw->height - src_h)
        return HV_AGX_SCANOUT_RESULT_LAYER_SOURCE_RECT;
    if (dst_w < HV_AGX_SCANOUT_V3_MIN_EXTENT ||
        dst_h < HV_AGX_SCANOUT_V3_MIN_EXTENT ||
        dst_w > HV_AGX_SCANOUT_J313_WIDTH ||
        dst_x > HV_AGX_SCANOUT_J313_WIDTH - dst_w ||
        dst_h > HV_AGX_SCANOUT_J313_HEIGHT ||
        dst_y > HV_AGX_SCANOUT_J313_HEIGHT - dst_h)
        return HV_AGX_SCANOUT_RESULT_LAYER_DEST_RECT;
    if (!scale_ok(src_w, dst_w) || !scale_ok(src_h, dst_h))
        return HV_AGX_SCANOUT_RESULT_LAYER_SCALE;
    *layer = (struct hv_agx_scanout_layer){
        .SurfaceOffset = raw->surface_offset,
        .SurfaceSize = raw->surface_size,
        .Width = raw->width,
        .Height = raw->height,
        .Stride = raw->stride,
        .Format = raw->format,
        .SrcX = src_x, .SrcY = src_y, .SrcWidth = src_w, .SrcHeight = src_h,
        .DstX = dst_x, .DstY = dst_y, .DstWidth = dst_w, .DstHeight = dst_h,
        .Flags = raw->flags,
    };
    return HV_AGX_SCANOUT_RESULT_OK;
}

/* V3: the whole commit -> validated layers, or the refusal code. */
static enum hv_agx_scanout_result
commit_validate(const struct hv_agx_scanout_broker *broker,
                struct hv_agx_scanout_layer layers[HV_AGX_SCANOUT_V3_LAYERS])
{
    uint32_t count = broker->staging_layer_count;
    enum hv_agx_scanout_result result;

    if (!(broker->negotiated & HV_AGX_SCANOUT_CAP_TWO_LAYERS))
        return HV_AGX_SCANOUT_RESULT_LAYERS_NOT_NEGOTIATED;
    if (count == 0 || count > HV_AGX_SCANOUT_V3_LAYERS)
        return HV_AGX_SCANOUT_RESULT_LAYER_COUNT;
    for (uint32_t i = 0; i < count; i++) {
        result = layer_validate(broker, &broker->staging_layers[i], &layers[i]);
        if (result != HV_AGX_SCANOUT_RESULT_OK)
            return result;
    }
    if (count == 2) {
        const struct hv_agx_scanout_layer *a = &layers[0];
        const struct hv_agx_scanout_layer *b = &layers[1];

        if (a->SurfaceOffset == b->SurfaceOffset)
            return HV_AGX_SCANOUT_RESULT_LAYER_SAME_SURFACE;
        /* Both ranges were proven inside the pool, so no sum overflows. */
        if (a->SurfaceOffset < b->SurfaceOffset + b->SurfaceSize &&
            b->SurfaceOffset < a->SurfaceOffset + a->SurfaceSize)
            return HV_AGX_SCANOUT_RESULT_LAYER_OVERLAP;
    }
    return HV_AGX_SCANOUT_RESULT_OK;
}

static void reset_destination(struct hv_agx_scanout_broker *broker)
{
    broker->staging_dest_origin = 0;
    broker->staging_dest_size = HV_AGX_SCANOUT_J313_DEST_SIZE;
}

static bool submit(struct hv_agx_scanout_broker *broker, uint32_t command)
{
    uint64_t sequence = broker->staging_sequence;
    struct hv_agx_scanout_layer layers[HV_AGX_SCANOUT_V3_LAYERS];
    enum hv_agx_scanout_result layer_result;

    if (!sequence_fresh(broker, sequence)) {
        reject(broker, HV_AGX_SCANOUT_RESULT_STALE_SEQUENCE);
        return true;
    }
    if (broker->state == HV_AGX_SCANOUT_PENDING ||
        broker->state == HV_AGX_SCANOUT_QUIESCING) {
        reject(broker, HV_AGX_SCANOUT_RESULT_BUSY);
        return true;
    }

    if (command == HV_AGX_SCANOUT_CMD_QUERY) {
        broker->highest_sequence = sequence;
        broker->receipt_sequence = sequence;
        broker->result = HV_AGX_SCANOUT_RESULT_OK;
        broker->accepted_requests++;
        return true;
    }
    if (command == HV_AGX_SCANOUT_CMD_REGISTER_POOL) {
        if (broker->state != HV_AGX_SCANOUT_UNREGISTERED ||
            !aligned(broker->staging_pool_ipa) ||
            broker->staging_pool_size != broker->pool_size ||
            add_overflows(broker->staging_pool_ipa, broker->staging_pool_size)) {
            reject_terminal(broker, HV_AGX_SCANOUT_RESULT_INVALID_POOL, sequence);
            return true;
        }
    } else if (command == HV_AGX_SCANOUT_CMD_PRESENT) {
        if ((broker->state != HV_AGX_SCANOUT_READY &&
             broker->state != HV_AGX_SCANOUT_ACTIVE) ||
            !surface_valid(broker)) {
            reject_terminal(broker, HV_AGX_SCANOUT_RESULT_INVALID_SURFACE, sequence);
            return true;
        }
        if (broker->state == HV_AGX_SCANOUT_ACTIVE &&
            broker->latched_sequence != broker->applied_sequence) {
            reject(broker, HV_AGX_SCANOUT_RESULT_BUSY);
            return true;
        }
        /* V3: the one-slot swap leaves DCP slot 1 untouched. */
        if (broker->slot1_latched) {
            reject_terminal(broker, HV_AGX_SCANOUT_RESULT_SLOT1_LATCHED, sequence);
            return true;
        }
    } else if (command == HV_AGX_SCANOUT_CMD_COMMIT_LAYERS &&
               two_layers_advertised(broker)) {
        if (broker->state != HV_AGX_SCANOUT_READY &&
            broker->state != HV_AGX_SCANOUT_ACTIVE) {
            reject_terminal(broker, HV_AGX_SCANOUT_RESULT_INVALID_SURFACE, sequence);
            return true;
        }
        memset(layers, 0, sizeof(layers));
        layer_result = commit_validate(broker, layers);
        if (layer_result != HV_AGX_SCANOUT_RESULT_OK) {
            reject_terminal(broker, layer_result, sequence);
            return true;
        }
        if (broker->state == HV_AGX_SCANOUT_ACTIVE &&
            broker->latched_sequence != broker->applied_sequence) {
            reject(broker, HV_AGX_SCANOUT_RESULT_BUSY);
            return true;
        }
    } else if (command == HV_AGX_SCANOUT_CMD_RELEASE) {
        if (broker->state != HV_AGX_SCANOUT_READY &&
            broker->state != HV_AGX_SCANOUT_ACTIVE) {
            reject(broker, HV_AGX_SCANOUT_RESULT_BUSY);
            return true;
        }
    } else {
        reject_terminal(broker, HV_AGX_SCANOUT_RESULT_INVALID_COMMAND, sequence);
        return true;
    }

    memset(&broker->pending, 0, sizeof(broker->pending));
    broker->pending.Command = command;
    broker->pending.Sequence = sequence;
    broker->pending.PoolIpa = broker->staging_pool_ipa;
    broker->pending.PoolSize = broker->staging_pool_size;
    broker->pending.SurfaceOffset = broker->staging_surface_offset;
    broker->pending.SurfaceSize = broker->staging_surface_size;
    broker->pending.Width = broker->staging_width;
    broker->pending.Height = broker->staging_height;
    broker->pending.Stride = broker->staging_stride;
    broker->pending.Format = broker->staging_format;
    if (scaled_source(broker)) {
        broker->pending.DestX = HV_AGX_SCANOUT_DEST_LOW(broker->staging_dest_origin);
        broker->pending.DestY = HV_AGX_SCANOUT_DEST_HIGH(broker->staging_dest_origin);
        broker->pending.DestWidth = HV_AGX_SCANOUT_DEST_LOW(broker->staging_dest_size);
        broker->pending.DestHeight = HV_AGX_SCANOUT_DEST_HIGH(broker->staging_dest_size);
    } else {
        broker->pending.DestX = 0;
        broker->pending.DestY = 0;
        broker->pending.DestWidth = HV_AGX_SCANOUT_J313_WIDTH;
        broker->pending.DestHeight = HV_AGX_SCANOUT_J313_HEIGHT;
    }
    if (command == HV_AGX_SCANOUT_CMD_COMMIT_LAYERS) {
        /* The V1/V2 fields describe record 0 (bounds checks, receipts). */
        broker->pending.LayerCount = broker->staging_layer_count;
        memcpy(broker->pending.Layers, layers, sizeof(layers));
        broker->pending.SurfaceOffset = layers[0].SurfaceOffset;
        broker->pending.SurfaceSize = layers[0].SurfaceSize;
        broker->pending.Width = layers[0].Width;
        broker->pending.Height = layers[0].Height;
        broker->pending.Stride = layers[0].Stride;
        broker->pending.Format = layers[0].Format;
        broker->pending.DestX = layers[0].DstX;
        broker->pending.DestY = layers[0].DstY;
        broker->pending.DestWidth = layers[0].DstWidth;
        broker->pending.DestHeight = layers[0].DstHeight;
        /* Slot-1 transition (a): a one-layer commit after a two-layer one
         * removes slot 1; every later one-layer commit leaves it alone. */
        broker->pending.RemoveSlot1 =
            broker->pending.LayerCount == 1 && broker->slot1_latched;
    } else if (command == HV_AGX_SCANOUT_CMD_RELEASE) {
        /* Slot-1 transition (b): RELEASE quiesces both slots. */
        broker->pending.RemoveSlot1 = broker->slot1_latched;
    }
    broker->state_before_pending = broker->state;
    broker->state = command == HV_AGX_SCANOUT_CMD_RELEASE ? HV_AGX_SCANOUT_QUIESCING
                                                          : HV_AGX_SCANOUT_PENDING;
    broker->highest_sequence = sequence;
    broker->pending_taken = false;
    broker->result = HV_AGX_SCANOUT_RESULT_OK;
    broker->accepted_requests++;
    return true;
}

void hv_agx_scanout_broker_init(struct hv_agx_scanout_broker *broker)
{
    if (!broker)
        return;
    memset(broker, 0, sizeof(*broker));
    broker->abi_version = HV_AGX_SCANOUT_ABI_VERSION_V1;
    broker->capabilities = SCANOUT_V1_CAPS;
    broker->state = HV_AGX_SCANOUT_UNREGISTERED;
    broker->state_before_pending = HV_AGX_SCANOUT_UNREGISTERED;
    broker->result = HV_AGX_SCANOUT_RESULT_OK;
    broker->pool_size = HV_AGX_SCANOUT_J313_POOL_SIZE;
    reset_destination(broker);
}

bool hv_agx_scanout_broker_set_pool_size(struct hv_agx_scanout_broker *broker,
                                         uint64_t bytes)
{
    if (!broker || broker->state != HV_AGX_SCANOUT_UNREGISTERED ||
        !aligned(bytes) || bytes < HV_AGX_SCANOUT_J313_SURFACE_SIZE ||
        bytes > HV_AGX_SCANOUT_J313_POOL_SIZE)
        return false;
    broker->pool_size = bytes;
    return true;
}

void hv_agx_scanout_broker_init_v2(struct hv_agx_scanout_broker *broker,
                                   bool latch_source_proven)
{
    hv_agx_scanout_broker_init(broker);
    if (!broker)
        return;
    broker->abi_version = HV_AGX_SCANOUT_ABI_VERSION_V2;
    /* rc106: the v2 owner carries the swap destination rectangle (iomfb
     * v13.5 destination[0], iBoot dst_rect), so it may scale a smaller source
     * into the fixed panel. */
    broker->capabilities |= HV_AGX_SCANOUT_CAP_SCALED_SOURCE;
    if (latch_source_proven)
        broker->capabilities |= HV_AGX_SCANOUT_V2_LATCH_CAPABILITIES;
}

bool hv_agx_scanout_broker_enable_two_layers(struct hv_agx_scanout_broker *broker)
{
    if (!broker || broker->abi_version != HV_AGX_SCANOUT_ABI_VERSION_V2 ||
        (broker->capabilities & HV_AGX_SCANOUT_V2_LATCH_CAPABILITIES) !=
            HV_AGX_SCANOUT_V2_LATCH_CAPABILITIES ||
        broker->state != HV_AGX_SCANOUT_UNREGISTERED)
        return false;
    broker->capabilities |= HV_AGX_SCANOUT_CAP_TWO_LAYERS;
    return true;
}

/* V3 registers; only decoded while CAP_TWO_LAYERS is advertised. */
static bool v3_layer_field(uint64_t offset, unsigned *index, uint64_t *field)
{
    if (offset < HV_AGX_SCANOUT_REG_LAYER0 ||
        offset >= HV_AGX_SCANOUT_REG_LAYER0 +
                      HV_AGX_SCANOUT_V3_LAYERS * HV_AGX_SCANOUT_V3_LAYER_RECORD_SIZE)
        return false;
    *index = (unsigned)((offset - HV_AGX_SCANOUT_REG_LAYER0) /
                        HV_AGX_SCANOUT_V3_LAYER_RECORD_SIZE);
    *field = (offset - HV_AGX_SCANOUT_REG_LAYER0) %
             HV_AGX_SCANOUT_V3_LAYER_RECORD_SIZE;
    return true;
}

static uint32_t *v3_layer_u32(struct hv_agx_scanout_layer_staging *layer,
                              uint64_t field)
{
    switch (field) {
    case HV_AGX_SCANOUT_LAYER_WIDTH: return &layer->width;
    case HV_AGX_SCANOUT_LAYER_HEIGHT: return &layer->height;
    case HV_AGX_SCANOUT_LAYER_STRIDE: return &layer->stride;
    case HV_AGX_SCANOUT_LAYER_FORMAT: return &layer->format;
    case HV_AGX_SCANOUT_LAYER_SRC_ORIGIN: return &layer->src_origin;
    case HV_AGX_SCANOUT_LAYER_SRC_SIZE: return &layer->src_size;
    case HV_AGX_SCANOUT_LAYER_DST_ORIGIN: return &layer->dst_origin;
    case HV_AGX_SCANOUT_LAYER_DST_SIZE: return &layer->dst_size;
    case HV_AGX_SCANOUT_LAYER_FLAGS: return &layer->flags;
    case HV_AGX_SCANOUT_LAYER_RESERVED0: return &layer->reserved0;
    default: return NULL;
    }
}

static uint64_t *v3_layer_u64(struct hv_agx_scanout_layer_staging *layer,
                              uint64_t field)
{
    switch (field) {
    case HV_AGX_SCANOUT_LAYER_SURFACE_OFFSET: return &layer->surface_offset;
    case HV_AGX_SCANOUT_LAYER_SURFACE_SIZE: return &layer->surface_size;
    case HV_AGX_SCANOUT_LAYER_RESERVED1: return &layer->reserved1;
    default: return NULL;
    }
}

static bool v3_mmio(struct hv_agx_scanout_broker *broker, uint64_t offset,
                    uint64_t *value, bool write, unsigned width)
{
    unsigned index;
    uint64_t field;

    if (!two_layers_advertised(broker) ||
        offset >= HV_AGX_SCANOUT_V3_MMIO_SIZE)
        return false;
    if (width == 2) {
        uint32_t *slot;

        if (offset & 3)
            return false;
        switch (offset) {
        case HV_AGX_SCANOUT_REG_NEGOTIATED:
            if (!write) {
                *value = broker->negotiated;
            } else if (broker->state == HV_AGX_SCANOUT_UNREGISTERED &&
                       ((uint32_t)*value & ~HV_AGX_SCANOUT_CAP_TWO_LAYERS) == 0 &&
                       (*value >> 32) == 0) {
                broker->negotiated = (uint32_t)*value;
            }
            return true;
        case HV_AGX_SCANOUT_REG_LAYER_COUNT:
            if (write)
                broker->staging_layer_count = (uint32_t)*value;
            else
                *value = broker->staging_layer_count;
            return true;
        case HV_AGX_SCANOUT_REG_ACTIVE_LAYERS:
            if (write)
                return false;
            *value = (broker->state == HV_AGX_SCANOUT_ACTIVE
                          ? HV_AGX_SCANOUT_ACTIVE_SLOT0 : 0u) |
                     (broker->slot1_latched ? HV_AGX_SCANOUT_ACTIVE_SLOT1 : 0u);
            return true;
        default:
            break;
        }
        if (!v3_layer_field(offset, &index, &field))
            return false;
        slot = v3_layer_u32(&broker->staging_layers[index], field);
        if (!slot)
            return false;
        if (write)
            *slot = (uint32_t)*value;
        else
            *value = *slot;
        return true;
    }
    if (width == 3) {
        uint64_t *slot;

        if (offset & 7)
            return false;
        if (offset == HV_AGX_SCANOUT_REG_ACTIVE_OFFSET_1) {
            if (write)
                return false;
            *value = broker->active_offset_1;
            return true;
        }
        if (!v3_layer_field(offset, &index, &field))
            return false;
        slot = v3_layer_u64(&broker->staging_layers[index], field);
        if (!slot)
            return false;
        if (write)
            *slot = *value;
        else
            *value = *slot;
        return true;
    }
    return false;
}

static bool read_u32(const struct hv_agx_scanout_broker *broker, uint64_t offset,
                     uint64_t *value)
{
    switch (offset) {
    case HV_AGX_SCANOUT_REG_MAGIC:
        *value = HV_AGX_SCANOUT_MAGIC;
        break;
    case HV_AGX_SCANOUT_REG_ABI_VERSION:
        *value = broker->abi_version;
        break;
    case HV_AGX_SCANOUT_REG_CAPABILITIES:
        *value = broker->capabilities;
        break;
    case HV_AGX_SCANOUT_REG_STATE:
        *value = broker->state;
        break;
    case HV_AGX_SCANOUT_REG_RESULT:
        *value = broker->result;
        break;
    case HV_AGX_SCANOUT_REG_IRQ_STATUS:
        *value = broker->irq_status;
        break;
    case HV_AGX_SCANOUT_REG_IRQ_ENABLE:
        *value = broker->irq_enable;
        break;
    case HV_AGX_SCANOUT_REG_SWAP_ID:
        *value = broker->swap_id;
        break;
    case HV_AGX_SCANOUT_REG_WIDTH:
        *value = broker->staging_width;
        break;
    case HV_AGX_SCANOUT_REG_HEIGHT:
        *value = broker->staging_height;
        break;
    case HV_AGX_SCANOUT_REG_STRIDE:
        *value = broker->staging_stride;
        break;
    case HV_AGX_SCANOUT_REG_FORMAT:
        *value = broker->staging_format;
        break;
    case HV_AGX_SCANOUT_REG_DEST_ORIGIN:
        *value = broker->staging_dest_origin;
        break;
    case HV_AGX_SCANOUT_REG_DEST_SIZE:
        *value = broker->staging_dest_size;
        break;
    default:
        return false;
    }
    return true;
}

static bool read_u64(const struct hv_agx_scanout_broker *broker, uint64_t offset,
                     uint64_t *value)
{
    switch (offset) {
    case HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE:
        *value = broker->receipt_sequence;
        break;
    case HV_AGX_SCANOUT_REG_APPLIED_SEQUENCE:
        *value = broker->applied_sequence;
        break;
    case HV_AGX_SCANOUT_REG_LATCHED_SEQUENCE:
        *value = broker->latched_sequence;
        break;
    case HV_AGX_SCANOUT_REG_ACTIVE_OFFSET:
        *value = broker->active_offset;
        break;
    case HV_AGX_SCANOUT_REG_POOL_PA:
        *value = broker->pool_pa;
        break;
    case HV_AGX_SCANOUT_REG_ACCEPTED_REQUESTS:
        *value = broker->accepted_requests;
        break;
    case HV_AGX_SCANOUT_REG_REJECTED_REQUESTS:
        *value = broker->rejected_requests;
        break;
    case HV_AGX_SCANOUT_REG_POOL_IPA:
        *value = broker->staging_pool_ipa;
        break;
    case HV_AGX_SCANOUT_REG_POOL_SIZE:
        *value = broker->staging_pool_size;
        break;
    case HV_AGX_SCANOUT_REG_SURFACE_OFFSET:
        *value = broker->staging_surface_offset;
        break;
    case HV_AGX_SCANOUT_REG_SURFACE_SIZE:
        *value = broker->staging_surface_size;
        break;
    case HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE:
        *value = broker->staging_sequence;
        break;
    default:
        return false;
    }
    return true;
}

bool hv_agx_scanout_broker_mmio(struct hv_agx_scanout_broker *broker, uint64_t offset,
                                uint64_t *value, bool write, unsigned width)
{
    if (!broker || !value)
        return false;
    if (offset >= HV_AGX_SCANOUT_MMIO_SIZE)
        return v3_mmio(broker, offset, value, write, width);
    if (width == 2) {
        if (offset & 3)
            return false;
        if (!write)
            return read_u32(broker, offset, value);
        switch (offset) {
        case HV_AGX_SCANOUT_REG_IRQ_STATUS:
            broker->irq_status &= ~((uint32_t)*value & HV_AGX_SCANOUT_IRQ_MASK);
            if ((broker->irq_status & broker->irq_enable) == 0)
                broker->irq_edge_taken = false;
            return true;
        case HV_AGX_SCANOUT_REG_IRQ_ENABLE:
            broker->irq_enable = (uint32_t)*value & HV_AGX_SCANOUT_IRQ_MASK;
            if ((broker->irq_status & broker->irq_enable) == 0)
                broker->irq_edge_taken = false;
            return true;
        case HV_AGX_SCANOUT_REG_WIDTH:
            broker->staging_width = (uint32_t)*value;
            return true;
        case HV_AGX_SCANOUT_REG_HEIGHT:
            broker->staging_height = (uint32_t)*value;
            return true;
        case HV_AGX_SCANOUT_REG_STRIDE:
            broker->staging_stride = (uint32_t)*value;
            return true;
        case HV_AGX_SCANOUT_REG_FORMAT:
            broker->staging_format = (uint32_t)*value;
            return true;
        case HV_AGX_SCANOUT_REG_DEST_ORIGIN:
            broker->staging_dest_origin = (uint32_t)*value;
            return true;
        case HV_AGX_SCANOUT_REG_DEST_SIZE:
            broker->staging_dest_size = (uint32_t)*value;
            return true;
        case HV_AGX_SCANOUT_REG_COMMAND:
            return submit(broker, (uint32_t)*value);
        default:
            return false;
        }
    }
    if (width == 3) {
        if (offset & 7)
            return false;
        if (!write)
            return read_u64(broker, offset, value);
        switch (offset) {
        case HV_AGX_SCANOUT_REG_POOL_IPA:
            broker->staging_pool_ipa = *value;
            return true;
        case HV_AGX_SCANOUT_REG_POOL_SIZE:
            broker->staging_pool_size = *value;
            return true;
        case HV_AGX_SCANOUT_REG_SURFACE_OFFSET:
            broker->staging_surface_offset = *value;
            return true;
        case HV_AGX_SCANOUT_REG_SURFACE_SIZE:
            broker->staging_surface_size = *value;
            return true;
        case HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE:
            broker->staging_sequence = *value;
            return true;
        default:
            return false;
        }
    }
    return false;
}

bool hv_agx_scanout_broker_take_pending(struct hv_agx_scanout_broker *broker,
                                        struct hv_agx_scanout_request *request)
{
    if (!broker || !request ||
        (broker->state != HV_AGX_SCANOUT_PENDING &&
         broker->state != HV_AGX_SCANOUT_QUIESCING) ||
        broker->pending_taken)
        return false;
    *request = broker->pending;
    broker->pending_taken = true;
    return true;
}

static bool completion_matches(const struct hv_agx_scanout_broker *broker,
                               uint64_t sequence, uint32_t command)
{
    return broker && broker->pending_taken && broker->pending.Sequence == sequence &&
           broker->pending.Command == command;
}

bool hv_agx_scanout_broker_complete_register(struct hv_agx_scanout_broker *broker,
                                             uint64_t sequence,
                                             enum hv_agx_scanout_result result,
                                             uint64_t pool_pa, uint64_t pool_iova)
{
    if (!completion_matches(broker, sequence, HV_AGX_SCANOUT_CMD_REGISTER_POOL))
        return false;
    if (result == HV_AGX_SCANOUT_RESULT_OK && aligned(pool_pa) && pool_iova != 0 &&
        (pool_iova & (UINT64_C(0x4000) - 1)) == 0 &&
        !add_overflows(pool_pa, broker->pending.PoolSize) &&
        !add_overflows(pool_iova, broker->pending.PoolSize)) {
        broker->registered_pool_ipa = broker->pending.PoolIpa;
        broker->registered_pool_size = broker->pending.PoolSize;
        broker->pool_pa = pool_pa;
        broker->pool_iova = pool_iova;
        broker->state = HV_AGX_SCANOUT_READY;
        broker->applied_sequence = sequence;
    } else {
        if (result == HV_AGX_SCANOUT_RESULT_OK)
            result = HV_AGX_SCANOUT_RESULT_MAP_FAILED;
        broker->state = HV_AGX_SCANOUT_UNREGISTERED;
        broker->registered_pool_ipa = 0;
        broker->registered_pool_size = 0;
        broker->pool_pa = 0;
        broker->pool_iova = 0;
    }
    broker->result = result;
    broker->receipt_sequence = sequence;
    broker->pending_taken = false;
    return true;
}

bool hv_agx_scanout_broker_complete_present(struct hv_agx_scanout_broker *broker,
                                            uint64_t sequence,
                                            enum hv_agx_scanout_result result,
                                            uint32_t swap_id)
{
    if (!completion_matches(broker, sequence, HV_AGX_SCANOUT_CMD_PRESENT) &&
        !completion_matches(broker, sequence, HV_AGX_SCANOUT_CMD_COMMIT_LAYERS))
        return false;
    /* Slot-1 transition (c): a two-layer commit that reached the service
     * may have put slot 1 on screen, even when its swap then failed, so the
     * next one-layer commit or RELEASE removes it. */
    if (broker->pending.Command == HV_AGX_SCANOUT_CMD_COMMIT_LAYERS &&
        broker->pending.LayerCount == 2) {
        broker->slot1_latched = true;
        broker->slot1_removal_sequence = 0;
    }
    if (result == HV_AGX_SCANOUT_RESULT_OK && swap_id != 0) {
        broker->active_offset = broker->pending.SurfaceOffset;
        broker->swap_id = swap_id;
        broker->applied_sequence = sequence;
        broker->state = HV_AGX_SCANOUT_ACTIVE;
        if (broker->pending.Command == HV_AGX_SCANOUT_CMD_COMMIT_LAYERS) {
            if (broker->pending.LayerCount == 2)
                broker->active_offset_1 = broker->pending.Layers[1].SurfaceOffset;
            else if (broker->pending.RemoveSlot1)
                broker->slot1_removal_sequence = sequence;
        }
    } else {
        if (result == HV_AGX_SCANOUT_RESULT_OK)
            result = HV_AGX_SCANOUT_RESULT_PRESENT_FAILED;
        broker->state = broker->state_before_pending;
        broker->irq_status |= HV_AGX_SCANOUT_IRQ_ERROR;
    }
    broker->result = result;
    broker->receipt_sequence = sequence;
    broker->pending_taken = false;
    return true;
}

bool hv_agx_scanout_broker_mark_latched(struct hv_agx_scanout_broker *broker,
                                        uint64_t sequence)
{
    if (!broker || broker->abi_version != HV_AGX_SCANOUT_ABI_VERSION_V2 ||
        (broker->capabilities & HV_AGX_SCANOUT_V2_LATCH_CAPABILITIES) !=
            HV_AGX_SCANOUT_V2_LATCH_CAPABILITIES ||
        broker->state != HV_AGX_SCANOUT_ACTIVE || sequence == 0 ||
        sequence != broker->applied_sequence ||
        sequence <= broker->latched_sequence || broker->swap_id == 0)
        return false;
    broker->latched_sequence = sequence;
    broker->irq_status |= HV_AGX_SCANOUT_IRQ_LATCHED;
    /* Slot-1 transition (d): the D589 of the removal swap. */
    if (broker->slot1_removal_sequence == sequence) {
        broker->slot1_latched = false;
        broker->slot1_removal_sequence = 0;
        broker->active_offset_1 = 0;
    }
    return true;
}

bool hv_agx_scanout_broker_fail_latch(struct hv_agx_scanout_broker *broker,
                                      uint64_t sequence)
{
    if (!broker || broker->abi_version != HV_AGX_SCANOUT_ABI_VERSION_V2 ||
        broker->state != HV_AGX_SCANOUT_ACTIVE || sequence == 0 ||
        sequence != broker->applied_sequence ||
        sequence <= broker->latched_sequence)
        return false;
    broker->result = HV_AGX_SCANOUT_RESULT_PRESENT_FAILED;
    broker->irq_status |= HV_AGX_SCANOUT_IRQ_ERROR;
    return true;
}

bool hv_agx_scanout_broker_take_irq_edge(struct hv_agx_scanout_broker *broker)
{
    if (!broker || broker->irq_edge_taken ||
        (broker->irq_status & broker->irq_enable) == 0)
        return false;
    broker->irq_edge_taken = true;
    return true;
}

bool hv_agx_scanout_broker_complete_release(struct hv_agx_scanout_broker *broker,
                                            uint64_t sequence,
                                            enum hv_agx_scanout_result result,
                                            bool quiesce_proven)
{
    if (!completion_matches(broker, sequence, HV_AGX_SCANOUT_CMD_RELEASE))
        return false;
    /* Only the bounded service may prove that scanout has quiesced. */
    if (result == HV_AGX_SCANOUT_RESULT_OK && !quiesce_proven)
        result = HV_AGX_SCANOUT_RESULT_NOT_QUIESCED;
    if (result == HV_AGX_SCANOUT_RESULT_OK) {
        broker->state = HV_AGX_SCANOUT_UNREGISTERED;
        broker->active_offset = 0;
        broker->pool_pa = 0;
        broker->pool_iova = 0;
        broker->registered_pool_ipa = 0;
        broker->registered_pool_size = 0;
        broker->applied_sequence = sequence;
        /* Slot-1 transition (e): the proven quiesce swap carried the slot-1
         * removal (pending.RemoveSlot1), and the next owner negotiates
         * again. */
        broker->slot1_latched = false;
        broker->slot1_removal_sequence = 0;
        broker->active_offset_1 = 0;
        broker->negotiated = 0;
    } else {
        broker->state = broker->state_before_pending;
        broker->irq_status |= HV_AGX_SCANOUT_IRQ_ERROR;
    }
    broker->result = result;
    broker->receipt_sequence = sequence;
    broker->pending_taken = false;
    return true;
}
