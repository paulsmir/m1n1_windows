/* SPDX-License-Identifier: MIT */

#ifndef HV_AGX_SCANOUT_SERVICE_H
#define HV_AGX_SCANOUT_SERVICE_H

#include "hv_agx_scanout_broker.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HV_AGX_SCANOUT_SERVICE_PAGE_SIZE UINT64_C(0x4000)
#define HV_AGX_SCANOUT_SERVICE_PAGES_PER_STEP 16u
#define HV_AGX_SCANOUT_SERVICE_CHUNK_SIZE                                      \
    (HV_AGX_SCANOUT_SERVICE_PAGE_SIZE * HV_AGX_SCANOUT_SERVICE_PAGES_PER_STEP)
#define HV_AGX_SCANOUT_SERVICE_PAGE_COUNT                                      \
    (HV_AGX_SCANOUT_J313_POOL_SIZE / HV_AGX_SCANOUT_SERVICE_PAGE_SIZE)

enum hv_agx_scanout_dart {
    HV_AGX_SCANOUT_DART_DISPLAY = 0,
    HV_AGX_SCANOUT_DART_DCP,
    HV_AGX_SCANOUT_DART_COUNT,
};

enum hv_agx_scanout_async_result {
    HV_AGX_SCANOUT_ASYNC_PENDING = 0,
    HV_AGX_SCANOUT_ASYNC_APPLIED,
    HV_AGX_SCANOUT_ASYNC_FAILED,
};

enum hv_agx_scanout_latch_result {
    HV_AGX_SCANOUT_LATCH_PENDING = 0,
    HV_AGX_SCANOUT_LATCHED,
    HV_AGX_SCANOUT_LATCH_WRONG_SWAP,
    HV_AGX_SCANOUT_LATCH_FAILED,
};

/*
 * Every callback must return promptly. map/unmap operate on no more than
 * HV_AGX_SCANOUT_SERVICE_CHUNK_SIZE bytes; present and quiesce are split into
 * begin/poll pairs so the service can run outside MMIO/FIQ context.
 */
struct hv_agx_scanout_platform_ops {
    bool (*translate)(void *opaque, uint64_t ipa, uint64_t *pa);
    bool (*is_ram)(void *opaque, uint64_t pa, uint64_t size);
    bool (*reserve_iova)(void *opaque, uint64_t size, uint64_t alignment,
                         uint64_t *iova);
    void (*free_iova)(void *opaque, uint64_t iova, uint64_t size);
    bool (*map)(void *opaque, enum hv_agx_scanout_dart dart, uint64_t iova,
                uint64_t pa, uint64_t size);
    void (*unmap)(void *opaque, enum hv_agx_scanout_dart dart, uint64_t iova,
                  uint64_t size);
    bool (*diagnostic_fill)(void *opaque, uint64_t surface_pa,
                            uint64_t surface_iova,
                            const struct hv_agx_scanout_request *request);
    void (*diagnostic_snapshot)(void *opaque, uint64_t surface_pa,
                                uint64_t surface_iova,
                                const struct hv_agx_scanout_request *request);
    uint64_t (*now_ms)(void *opaque);
    void (*diagnostic_late_snapshot)(
        void *opaque, uint64_t surface_pa, uint64_t surface_iova,
        const struct hv_agx_scanout_request *request);
    bool (*present_begin)(void *opaque, uint64_t surface_iova,
                          const struct hv_agx_scanout_request *request,
                          uint64_t *cookie);
    enum hv_agx_scanout_async_result (*present_poll)(void *opaque,
                                                     uint64_t cookie,
                                                     uint32_t *applied_swap_id);
    enum hv_agx_scanout_latch_result (*present_latch_poll)(
        void *opaque, uint32_t expected_swap_id);
    bool (*quiesce_begin)(void *opaque, uint64_t *cookie);
    enum hv_agx_scanout_async_result (*quiesce_poll)(void *opaque,
                                                     uint64_t cookie);
};

enum hv_agx_scanout_service_state {
    HV_AGX_SCANOUT_SERVICE_IDLE = 0,
    HV_AGX_SCANOUT_SERVICE_VALIDATE,
    HV_AGX_SCANOUT_SERVICE_RESERVE_IOVA,
    HV_AGX_SCANOUT_SERVICE_MAP_DISPLAY,
    HV_AGX_SCANOUT_SERVICE_MAP_DCP,
    HV_AGX_SCANOUT_SERVICE_COMPLETE_REGISTER,
    HV_AGX_SCANOUT_SERVICE_ROLLBACK_DCP,
    HV_AGX_SCANOUT_SERVICE_ROLLBACK_DISPLAY,
    HV_AGX_SCANOUT_SERVICE_COMPLETE_REGISTER_ERROR,
    HV_AGX_SCANOUT_SERVICE_PRESENT_BEGIN,
    HV_AGX_SCANOUT_SERVICE_PRESENT_POLL,
    HV_AGX_SCANOUT_SERVICE_PRESENT_LATCH_POLL,
    HV_AGX_SCANOUT_SERVICE_QUIESCE_BEGIN,
    HV_AGX_SCANOUT_SERVICE_QUIESCE_POLL,
    HV_AGX_SCANOUT_SERVICE_UNMAP_DCP,
    HV_AGX_SCANOUT_SERVICE_UNMAP_DISPLAY,
    HV_AGX_SCANOUT_SERVICE_COMPLETE_RELEASE,
};

enum hv_agx_scanout_service_step_result {
    HV_AGX_SCANOUT_SERVICE_IDLE_STEP = 0,
    HV_AGX_SCANOUT_SERVICE_PROGRESSED,
    HV_AGX_SCANOUT_SERVICE_WAITING,
};

struct hv_agx_scanout_service {
    const struct hv_agx_scanout_platform_ops *ops;
    void *opaque;
    enum hv_agx_scanout_service_state state;
    enum hv_agx_scanout_result register_error;
    struct hv_agx_scanout_request request;
    uint64_t pool_pa;
    uint64_t pool_iova;
    uint64_t validated_bytes;
    uint64_t mapped_bytes[HV_AGX_SCANOUT_DART_COUNT];
    uint64_t unmapped_bytes[HV_AGX_SCANOUT_DART_COUNT];
    uint64_t async_cookie;
    uint32_t applied_swap_id;
    bool owns_pool;
    bool late_snapshot_armed;
    uint64_t late_snapshot_due_ms;
    uint64_t late_snapshot_pa;
    uint64_t late_snapshot_iova;
    struct hv_agx_scanout_request late_snapshot_request;
};

void hv_agx_scanout_service_init(struct hv_agx_scanout_service *service,
                                 const struct hv_agx_scanout_platform_ops *ops,
                                 void *opaque);
bool hv_agx_scanout_fill_bgra_stripes(void *pixels, uint32_t width,
                                      uint32_t height, uint32_t stride,
                                      uint64_t bytes);
struct hv_agx_scanout_pixel_stats {
    uint64_t pixel_count;
    uint64_t nonzero_pixels;
    uint64_t channel_sum[4]; /* B, G, R, A */
    uint64_t hash;
    uint32_t corners[4]; /* top left, top right, bottom left, bottom right */
};
bool hv_agx_scanout_pixel_stats(const void *pixels, uint32_t width,
                                 uint32_t height, uint32_t stride,
                                 uint64_t bytes,
                                 struct hv_agx_scanout_pixel_stats *stats);
enum hv_agx_scanout_service_step_result
hv_agx_scanout_service_step(struct hv_agx_scanout_service *service,
                            struct hv_agx_scanout_broker *broker);

#endif
