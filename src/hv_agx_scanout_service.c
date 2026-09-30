/* SPDX-License-Identifier: MIT */

#include "hv_agx_scanout_service.h"

#include <string.h>

bool hv_agx_scanout_pixel_stats(const void *pixels, uint32_t width,
                                 uint32_t height, uint32_t stride,
                                 uint64_t bytes,
                                 struct hv_agx_scanout_pixel_stats *stats)
{
    const uint8_t *base = pixels;
    if (!base || !stats || !width || !height || width > UINT32_MAX / 4u ||
        stride < width * 4u || (stride & 3u) ||
        (uint64_t)stride * height != bytes)
        return false;
    memset(stats, 0, sizeof(*stats));
    stats->pixel_count = (uint64_t)width * height;
    stats->hash = UINT64_C(0xcbf29ce484222325);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t *row = base + (uint64_t)y * stride;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t *pixel = row + (uint64_t)x * 4u;
            uint32_t value = (uint32_t)pixel[0] | ((uint32_t)pixel[1] << 8) |
                             ((uint32_t)pixel[2] << 16) |
                             ((uint32_t)pixel[3] << 24);
            stats->nonzero_pixels += value != 0;
            for (unsigned channel = 0; channel < 4; ++channel) {
                stats->channel_sum[channel] += pixel[channel];
                stats->hash ^= pixel[channel];
                stats->hash *= UINT64_C(0x100000001b3);
            }
            if (y == 0 && x == 0)
                stats->corners[0] = value;
            if (y == 0 && x == width - 1)
                stats->corners[1] = value;
            if (y == height - 1 && x == 0)
                stats->corners[2] = value;
            if (y == height - 1 && x == width - 1)
                stats->corners[3] = value;
        }
    }
    return true;
}

bool hv_agx_scanout_fill_bgra_stripes(void *pixels, uint32_t width,
                                      uint32_t height, uint32_t stride,
                                      uint64_t bytes)
{
    static const uint32_t colors[4] = {
        UINT32_C(0xffff0000), UINT32_C(0xff00ff00),
        UINT32_C(0xff0000ff), UINT32_C(0xffffffff),
    };
    uint8_t *base = pixels;

    if (!base || !width || !height || width > UINT32_MAX / 4u ||
        stride < width * 4u || (stride & 3u) ||
        (uint64_t)stride * height != bytes)
        return false;
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t *row = (uint32_t *)(base + (uint64_t)y * stride);
        for (uint32_t x = 0; x < width; ++x)
            row[x] = colors[((uint64_t)x * 4u) / width];
    }
    return true;
}

static uint64_t chunk_size(uint64_t completed, uint64_t total)
{
    uint64_t remaining = total - completed;

    return remaining < HV_AGX_SCANOUT_SERVICE_CHUNK_SIZE
               ? remaining
               : HV_AGX_SCANOUT_SERVICE_CHUNK_SIZE;
}

static void complete_register_error(struct hv_agx_scanout_service *service,
                                    struct hv_agx_scanout_broker *broker)
{
    hv_agx_scanout_broker_complete_register(
        broker, service->request.Sequence, service->register_error, 0, 0);
    service->state = HV_AGX_SCANOUT_SERVICE_IDLE;
}

static void start_rollback(struct hv_agx_scanout_service *service)
{
    service->register_error = HV_AGX_SCANOUT_RESULT_MAP_FAILED;
    service->state = service->mapped_bytes[HV_AGX_SCANOUT_DART_DCP]
                         ? HV_AGX_SCANOUT_SERVICE_ROLLBACK_DCP
                         : HV_AGX_SCANOUT_SERVICE_ROLLBACK_DISPLAY;
}

static void fail_present(struct hv_agx_scanout_service *service,
                         struct hv_agx_scanout_broker *broker)
{
    hv_agx_scanout_broker_complete_present(
        broker, service->request.Sequence, HV_AGX_SCANOUT_RESULT_PRESENT_FAILED, 0);
    service->state = HV_AGX_SCANOUT_SERVICE_IDLE;
}

static void fail_release(struct hv_agx_scanout_service *service,
                         struct hv_agx_scanout_broker *broker)
{
    hv_agx_scanout_broker_complete_release(
        broker, service->request.Sequence, HV_AGX_SCANOUT_RESULT_NOT_QUIESCED,
        false);
    service->state = HV_AGX_SCANOUT_SERVICE_IDLE;
}

void hv_agx_scanout_service_init(struct hv_agx_scanout_service *service,
                                 const struct hv_agx_scanout_platform_ops *ops,
                                 void *opaque)
{
    if (!service)
        return;
    memset(service, 0, sizeof(*service));
    service->ops = ops;
    service->opaque = opaque;
    service->state = HV_AGX_SCANOUT_SERVICE_IDLE;
}

static enum hv_agx_scanout_service_step_result
take_request(struct hv_agx_scanout_service *service,
             struct hv_agx_scanout_broker *broker)
{
    if (!hv_agx_scanout_broker_take_pending(broker, &service->request))
        return HV_AGX_SCANOUT_SERVICE_IDLE_STEP;

    switch (service->request.Command) {
    case HV_AGX_SCANOUT_CMD_REGISTER_POOL:
        service->validated_bytes = 0;
        service->pool_pa = 0;
        service->pool_iova = 0;
        memset(service->mapped_bytes, 0, sizeof(service->mapped_bytes));
        memset(service->unmapped_bytes, 0, sizeof(service->unmapped_bytes));
        service->state = HV_AGX_SCANOUT_SERVICE_VALIDATE;
        break;
    case HV_AGX_SCANOUT_CMD_PRESENT:
        service->state = HV_AGX_SCANOUT_SERVICE_PRESENT_BEGIN;
        break;
    case HV_AGX_SCANOUT_CMD_RELEASE:
        service->state = HV_AGX_SCANOUT_SERVICE_QUIESCE_BEGIN;
        break;
    default:
        return HV_AGX_SCANOUT_SERVICE_IDLE_STEP;
    }
    return HV_AGX_SCANOUT_SERVICE_PROGRESSED;
}

static void validate_pages(struct hv_agx_scanout_service *service)
{
    const struct hv_agx_scanout_platform_ops *ops = service->ops;

    if (!ops || !ops->translate || !ops->is_ram) {
        service->register_error = HV_AGX_SCANOUT_RESULT_UNMAPPED;
        service->state = HV_AGX_SCANOUT_SERVICE_COMPLETE_REGISTER_ERROR;
        return;
    }

    for (unsigned page = 0; page < HV_AGX_SCANOUT_SERVICE_PAGES_PER_STEP &&
                            service->validated_bytes < service->request.PoolSize;
         page++) {
        uint64_t ipa = service->request.PoolIpa + service->validated_bytes;
        uint64_t pa = 0;

        if (!ops->translate(service->opaque, ipa, &pa)) {
            service->register_error = HV_AGX_SCANOUT_RESULT_UNMAPPED;
            service->state = HV_AGX_SCANOUT_SERVICE_COMPLETE_REGISTER_ERROR;
            return;
        }
        if (!ops->is_ram(service->opaque, pa,
                         HV_AGX_SCANOUT_SERVICE_PAGE_SIZE)) {
            service->register_error = HV_AGX_SCANOUT_RESULT_NOT_RAM;
            service->state = HV_AGX_SCANOUT_SERVICE_COMPLETE_REGISTER_ERROR;
            return;
        }
        if ((pa & (HV_AGX_SCANOUT_SERVICE_PAGE_SIZE - 1)) != 0 ||
            (!service->validated_bytes &&
             pa > UINT64_MAX - service->request.PoolSize) ||
            (service->validated_bytes &&
             pa != service->pool_pa + service->validated_bytes)) {
            service->register_error = HV_AGX_SCANOUT_RESULT_NONCONTIGUOUS;
            service->state = HV_AGX_SCANOUT_SERVICE_COMPLETE_REGISTER_ERROR;
            return;
        }
        if (!service->validated_bytes)
            service->pool_pa = pa;
        service->validated_bytes += HV_AGX_SCANOUT_SERVICE_PAGE_SIZE;
    }

    if (service->validated_bytes == service->request.PoolSize)
        service->state = HV_AGX_SCANOUT_SERVICE_RESERVE_IOVA;
}

static void reserve_iova(struct hv_agx_scanout_service *service)
{
    const struct hv_agx_scanout_platform_ops *ops = service->ops;
    bool reserved;

    if (!ops || !ops->reserve_iova || !ops->free_iova) {
        service->register_error = HV_AGX_SCANOUT_RESULT_MAP_FAILED;
        service->state = HV_AGX_SCANOUT_SERVICE_COMPLETE_REGISTER_ERROR;
        return;
    }
    reserved = ops->reserve_iova(service->opaque, service->request.PoolSize,
                                 HV_AGX_SCANOUT_SERVICE_PAGE_SIZE,
                                 &service->pool_iova);
    if (!reserved || !service->pool_iova ||
        (service->pool_iova & (HV_AGX_SCANOUT_SERVICE_PAGE_SIZE - 1)) != 0) {
        if (reserved)
            ops->free_iova(service->opaque, service->pool_iova,
                           service->request.PoolSize);
        service->pool_iova = 0;
        service->register_error = HV_AGX_SCANOUT_RESULT_MAP_FAILED;
        service->state = HV_AGX_SCANOUT_SERVICE_COMPLETE_REGISTER_ERROR;
        return;
    }
    service->owns_pool = true;
    service->state = HV_AGX_SCANOUT_SERVICE_MAP_DISPLAY;
}

static void map_chunk(struct hv_agx_scanout_service *service,
                      enum hv_agx_scanout_dart dart)
{
    uint64_t completed = service->mapped_bytes[dart];
    uint64_t size = chunk_size(completed, service->request.PoolSize);

    if (!service->ops || !service->ops->map ||
        !service->ops->map(service->opaque, dart, service->pool_iova + completed,
                           service->pool_pa + completed, size)) {
        start_rollback(service);
        return;
    }
    service->mapped_bytes[dart] += size;
    if (service->mapped_bytes[dart] != service->request.PoolSize)
        return;
    service->state = dart == HV_AGX_SCANOUT_DART_DISPLAY
                         ? HV_AGX_SCANOUT_SERVICE_MAP_DCP
                         : HV_AGX_SCANOUT_SERVICE_COMPLETE_REGISTER;
}

static void rollback_chunk(struct hv_agx_scanout_service *service,
                           enum hv_agx_scanout_dart dart)
{
    uint64_t remaining = service->mapped_bytes[dart] - service->unmapped_bytes[dart];
    uint64_t size;

    if (!remaining) {
        service->state = dart == HV_AGX_SCANOUT_DART_DCP
                             ? HV_AGX_SCANOUT_SERVICE_ROLLBACK_DISPLAY
                             : HV_AGX_SCANOUT_SERVICE_COMPLETE_REGISTER_ERROR;
        return;
    }
    size = remaining < HV_AGX_SCANOUT_SERVICE_CHUNK_SIZE
               ? remaining
               : HV_AGX_SCANOUT_SERVICE_CHUNK_SIZE;
    service->ops->unmap(service->opaque, dart,
                        service->pool_iova + remaining - size, size);
    service->unmapped_bytes[dart] += size;
}

static void finish_rollback(struct hv_agx_scanout_service *service,
                            struct hv_agx_scanout_broker *broker)
{
    service->ops->free_iova(service->opaque, service->pool_iova,
                            service->request.PoolSize);
    service->pool_iova = 0;
    service->owns_pool = false;
    complete_register_error(service, broker);
}

static enum hv_agx_scanout_service_step_result
poll_present(struct hv_agx_scanout_service *service,
             struct hv_agx_scanout_broker *broker)
{
    uint32_t swap_id = 0;
    enum hv_agx_scanout_async_result result = service->ops->present_poll(
        service->opaque, service->async_cookie, &swap_id);

    if (result == HV_AGX_SCANOUT_ASYNC_PENDING)
        return HV_AGX_SCANOUT_SERVICE_WAITING;
    if (result != HV_AGX_SCANOUT_ASYNC_APPLIED || !swap_id) {
        fail_present(service, broker);
        return HV_AGX_SCANOUT_SERVICE_PROGRESSED;
    }
    if (!hv_agx_scanout_broker_complete_present(
            broker, service->request.Sequence, HV_AGX_SCANOUT_RESULT_OK,
            swap_id)) {
        service->state = HV_AGX_SCANOUT_SERVICE_IDLE;
        return HV_AGX_SCANOUT_SERVICE_PROGRESSED;
    }
    service->applied_swap_id = swap_id;
    if (broker->abi_version == HV_AGX_SCANOUT_ABI_VERSION_V2 &&
        (broker->capabilities & HV_AGX_SCANOUT_V2_LATCH_CAPABILITIES) ==
            HV_AGX_SCANOUT_V2_LATCH_CAPABILITIES)
        service->state = HV_AGX_SCANOUT_SERVICE_PRESENT_LATCH_POLL;
    else
        service->state = HV_AGX_SCANOUT_SERVICE_IDLE;
    return HV_AGX_SCANOUT_SERVICE_PROGRESSED;
}

static enum hv_agx_scanout_service_step_result
poll_present_latch(struct hv_agx_scanout_service *service,
                   struct hv_agx_scanout_broker *broker)
{
    enum hv_agx_scanout_latch_result result;

    if (!service->ops || !service->ops->present_latch_poll) {
        (void)hv_agx_scanout_broker_fail_latch(
            broker, service->request.Sequence);
        service->state = HV_AGX_SCANOUT_SERVICE_IDLE;
        return HV_AGX_SCANOUT_SERVICE_PROGRESSED;
    }
    result = service->ops->present_latch_poll(service->opaque,
                                               service->applied_swap_id);
    if (result == HV_AGX_SCANOUT_LATCH_PENDING)
        return HV_AGX_SCANOUT_SERVICE_WAITING;
    if (result == HV_AGX_SCANOUT_LATCHED) {
        bool latched = hv_agx_scanout_broker_mark_latched(
            broker, service->request.Sequence);
        if (latched && service->ops->now_ms &&
            (service->ops->diagnostic_late_snapshot ||
             service->ops->diagnostic_window_snapshot)) {
            uint64_t now = service->ops->now_ms(service->opaque);
            if (now <= UINT64_MAX - UINT64_C(600000)) {
                service->late_snapshot_due_ms = now + UINT64_C(15000);
                service->late_snapshot_pa =
                    service->pool_pa + service->request.SurfaceOffset;
                service->late_snapshot_iova =
                    service->pool_iova + service->request.SurfaceOffset;
                service->late_snapshot_request = service->request;
                service->late_snapshot_armed =
                    service->ops->diagnostic_late_snapshot != NULL;
                service->window_latch_ms = now;
                service->window_snapshot_index = 0;
                service->window_snapshot_armed =
                    service->ops->diagnostic_window_snapshot != NULL;
            }
        }
    } else
        (void)hv_agx_scanout_broker_fail_latch(
            broker, service->request.Sequence);
    service->state = HV_AGX_SCANOUT_SERVICE_IDLE;
    return HV_AGX_SCANOUT_SERVICE_PROGRESSED;
}

static void unmap_release_chunk(struct hv_agx_scanout_service *service,
                                enum hv_agx_scanout_dart dart)
{
    uint64_t remaining = service->mapped_bytes[dart] - service->unmapped_bytes[dart];
    uint64_t size;

    if (!remaining) {
        service->state = dart == HV_AGX_SCANOUT_DART_DCP
                             ? HV_AGX_SCANOUT_SERVICE_UNMAP_DISPLAY
                             : HV_AGX_SCANOUT_SERVICE_COMPLETE_RELEASE;
        return;
    }
    size = remaining < HV_AGX_SCANOUT_SERVICE_CHUNK_SIZE
               ? remaining
               : HV_AGX_SCANOUT_SERVICE_CHUNK_SIZE;
    service->ops->unmap(service->opaque, dart,
                        service->pool_iova + remaining - size, size);
    service->unmapped_bytes[dart] += size;
}

enum hv_agx_scanout_service_step_result
hv_agx_scanout_service_step(struct hv_agx_scanout_service *service,
                            struct hv_agx_scanout_broker *broker)
{
    enum hv_agx_scanout_async_result async_result;

    if (!service || !broker)
        return HV_AGX_SCANOUT_SERVICE_IDLE_STEP;

    switch (service->state) {
    case HV_AGX_SCANOUT_SERVICE_IDLE:
        if (service->late_snapshot_armed) {
            if (!service->owns_pool || broker->state != HV_AGX_SCANOUT_ACTIVE ||
                broker->latched_sequence !=
                    service->late_snapshot_request.Sequence) {
                service->late_snapshot_armed = false;
            } else if (service->ops && service->ops->now_ms &&
                       service->ops->diagnostic_late_snapshot &&
                       service->ops->now_ms(service->opaque) >=
                           service->late_snapshot_due_ms) {
                service->late_snapshot_armed = false;
                service->ops->diagnostic_late_snapshot(
                    service->opaque, service->late_snapshot_pa,
                    service->late_snapshot_iova,
                    &service->late_snapshot_request);
                return HV_AGX_SCANOUT_SERVICE_PROGRESSED;
            }
        }
        if (service->window_snapshot_armed) {
            static const uint64_t checkpoints_ms[] = {
                UINT64_C(120000), UINT64_C(300000), UINT64_C(600000),
            };
            if (!service->owns_pool || broker->state != HV_AGX_SCANOUT_ACTIVE ||
                broker->latched_sequence !=
                    service->late_snapshot_request.Sequence) {
                service->window_snapshot_armed = false;
            } else if (service->ops->now_ms &&
                       service->ops->diagnostic_window_snapshot) {
                uint64_t now = service->ops->now_ms(service->opaque);
                if (now >= service->window_latch_ms +
                           checkpoints_ms[service->window_snapshot_index]) {
                    service->ops->diagnostic_window_snapshot(
                        service->opaque, service->late_snapshot_pa,
                        service->late_snapshot_iova,
                        &service->late_snapshot_request,
                        service->applied_swap_id,
                        now - service->window_latch_ms);
                    if (++service->window_snapshot_index ==
                        sizeof(checkpoints_ms) / sizeof(checkpoints_ms[0]))
                        service->window_snapshot_armed = false;
                    return HV_AGX_SCANOUT_SERVICE_PROGRESSED;
                }
            }
        }
        return take_request(service, broker);
    case HV_AGX_SCANOUT_SERVICE_VALIDATE:
        validate_pages(service);
        break;
    case HV_AGX_SCANOUT_SERVICE_RESERVE_IOVA:
        reserve_iova(service);
        break;
    case HV_AGX_SCANOUT_SERVICE_MAP_DISPLAY:
        map_chunk(service, HV_AGX_SCANOUT_DART_DISPLAY);
        break;
    case HV_AGX_SCANOUT_SERVICE_MAP_DCP:
        map_chunk(service, HV_AGX_SCANOUT_DART_DCP);
        break;
    case HV_AGX_SCANOUT_SERVICE_COMPLETE_REGISTER:
        hv_agx_scanout_broker_complete_register(
            broker, service->request.Sequence, HV_AGX_SCANOUT_RESULT_OK,
            service->pool_pa, service->pool_iova);
        service->state = HV_AGX_SCANOUT_SERVICE_IDLE;
        break;
    case HV_AGX_SCANOUT_SERVICE_ROLLBACK_DCP:
        rollback_chunk(service, HV_AGX_SCANOUT_DART_DCP);
        break;
    case HV_AGX_SCANOUT_SERVICE_ROLLBACK_DISPLAY:
        rollback_chunk(service, HV_AGX_SCANOUT_DART_DISPLAY);
        break;
    case HV_AGX_SCANOUT_SERVICE_COMPLETE_REGISTER_ERROR:
        if (service->owns_pool)
            finish_rollback(service, broker);
        else
            complete_register_error(service, broker);
        break;
    case HV_AGX_SCANOUT_SERVICE_PRESENT_BEGIN:
        if (!service->owns_pool || !service->ops || !service->ops->present_begin ||
            !service->ops->present_poll ||
            service->request.SurfaceSize > service->request.PoolSize ||
            service->request.SurfaceOffset >
                service->request.PoolSize - service->request.SurfaceSize ||
            service->pool_pa > UINT64_MAX - service->request.SurfaceOffset ||
            service->pool_iova > UINT64_MAX - service->request.SurfaceOffset) {
            fail_present(service, broker);
            break;
        }
        if (service->ops->diagnostic_snapshot)
            service->ops->diagnostic_snapshot(
                service->opaque,
                service->pool_pa + service->request.SurfaceOffset,
                service->pool_iova + service->request.SurfaceOffset,
                &service->request);
        if ((service->ops->diagnostic_fill &&
             !service->ops->diagnostic_fill(
                 service->opaque,
                 service->pool_pa + service->request.SurfaceOffset,
                 service->pool_iova + service->request.SurfaceOffset,
                 &service->request)) ||
            !service->ops->present_begin(
                service->opaque,
                service->pool_iova + service->request.SurfaceOffset,
                &service->request, &service->async_cookie)) {
            fail_present(service, broker);
            break;
        }
        service->state = HV_AGX_SCANOUT_SERVICE_PRESENT_POLL;
        break;
    case HV_AGX_SCANOUT_SERVICE_PRESENT_POLL:
        return poll_present(service, broker);
    case HV_AGX_SCANOUT_SERVICE_PRESENT_LATCH_POLL:
        return poll_present_latch(service, broker);
    case HV_AGX_SCANOUT_SERVICE_QUIESCE_BEGIN:
        if (!service->owns_pool || !service->ops || !service->ops->quiesce_begin ||
            !service->ops->quiesce_poll || !service->ops->unmap ||
            !service->ops->free_iova ||
            !service->ops->quiesce_begin(service->opaque,
                                         &service->async_cookie)) {
            fail_release(service, broker);
            break;
        }
        service->state = HV_AGX_SCANOUT_SERVICE_QUIESCE_POLL;
        break;
    case HV_AGX_SCANOUT_SERVICE_QUIESCE_POLL:
        async_result = service->ops->quiesce_poll(service->opaque,
                                                  service->async_cookie);
        if (async_result == HV_AGX_SCANOUT_ASYNC_PENDING)
            return HV_AGX_SCANOUT_SERVICE_WAITING;
        if (async_result != HV_AGX_SCANOUT_ASYNC_APPLIED) {
            fail_release(service, broker);
            break;
        }
        memset(service->unmapped_bytes, 0, sizeof(service->unmapped_bytes));
        service->state = HV_AGX_SCANOUT_SERVICE_UNMAP_DCP;
        break;
    case HV_AGX_SCANOUT_SERVICE_UNMAP_DCP:
        unmap_release_chunk(service, HV_AGX_SCANOUT_DART_DCP);
        break;
    case HV_AGX_SCANOUT_SERVICE_UNMAP_DISPLAY:
        unmap_release_chunk(service, HV_AGX_SCANOUT_DART_DISPLAY);
        break;
    case HV_AGX_SCANOUT_SERVICE_COMPLETE_RELEASE:
        service->ops->free_iova(service->opaque, service->pool_iova,
                                service->request.PoolSize);
        service->owns_pool = false;
        service->pool_pa = 0;
        service->pool_iova = 0;
        memset(service->mapped_bytes, 0, sizeof(service->mapped_bytes));
        memset(service->unmapped_bytes, 0, sizeof(service->unmapped_bytes));
        hv_agx_scanout_broker_complete_release(
            broker, service->request.Sequence, HV_AGX_SCANOUT_RESULT_OK, true);
        service->state = HV_AGX_SCANOUT_SERVICE_IDLE;
        break;
    }
    return HV_AGX_SCANOUT_SERVICE_PROGRESSED;
}
