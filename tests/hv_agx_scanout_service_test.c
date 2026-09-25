/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/hv_agx_scanout_service.h"

struct fake_platform {
    uint64_t ipa_base;
    uint64_t pa_base;
    uint64_t iova;
    uint32_t translate_calls;
    uint32_t translate_calls_this_step;
    uint32_t max_translate_calls_per_step;
    int32_t unmapped_page;
    int32_t non_ram_page;
    int32_t noncontiguous_page;
    uint32_t reserve_calls;
    uint32_t free_calls;
    uint32_t map_calls[HV_AGX_SCANOUT_DART_COUNT];
    uint32_t unmap_calls[HV_AGX_SCANOUT_DART_COUNT];
    uint64_t mapped_bytes[HV_AGX_SCANOUT_DART_COUNT];
    uint64_t unmapped_bytes[HV_AGX_SCANOUT_DART_COUNT];
    int32_t fail_map_dart;
    uint32_t fail_map_call;
    uint32_t present_begin_calls;
    uint32_t diagnostic_calls;
    uint32_t snapshot_calls;
    uint64_t snapshot_pa;
    uint64_t snapshot_iova;
    uint64_t diagnostic_pa;
    uint64_t diagnostic_iova;
    bool diagnostic_fail;
    uint32_t present_poll_calls;
    enum hv_agx_scanout_async_result present_result;
    uint32_t applied_swap_id;
    uint32_t latch_poll_calls;
    uint32_t latch_expected_swap_id;
    enum hv_agx_scanout_latch_result latch_result;
    uint64_t presented_iova;
    uint32_t quiesce_begin_calls;
    uint32_t quiesce_poll_calls;
    bool quiesce_begin_ok;
    enum hv_agx_scanout_async_result quiesce_result;
};

static bool fake_translate(void *opaque, uint64_t ipa, uint64_t *pa)
{
    struct fake_platform *fake = opaque;
    uint64_t page = (ipa - fake->ipa_base) / HV_AGX_SCANOUT_SERVICE_PAGE_SIZE;

    fake->translate_calls++;
    fake->translate_calls_this_step++;
    if ((int32_t)page == fake->unmapped_page)
        return false;
    *pa = fake->pa_base + page * HV_AGX_SCANOUT_SERVICE_PAGE_SIZE;
    if ((int32_t)page >= fake->noncontiguous_page && fake->noncontiguous_page >= 0)
        *pa += HV_AGX_SCANOUT_SERVICE_PAGE_SIZE;
    return true;
}

static bool fake_is_ram(void *opaque, uint64_t pa, uint64_t size)
{
    struct fake_platform *fake = opaque;
    uint64_t page = (pa - fake->pa_base) / HV_AGX_SCANOUT_SERVICE_PAGE_SIZE;

    assert(size == HV_AGX_SCANOUT_SERVICE_PAGE_SIZE);
    return (int32_t)page != fake->non_ram_page;
}

static bool fake_reserve_iova(void *opaque, uint64_t size, uint64_t alignment,
                              uint64_t *iova)
{
    struct fake_platform *fake = opaque;

    fake->reserve_calls++;
    assert(size == HV_AGX_SCANOUT_J313_POOL_SIZE);
    assert(alignment == HV_AGX_SCANOUT_SERVICE_PAGE_SIZE);
    *iova = fake->iova;
    return fake->iova != 0;
}

static void fake_free_iova(void *opaque, uint64_t iova, uint64_t size)
{
    struct fake_platform *fake = opaque;

    assert(iova == fake->iova);
    assert(size == HV_AGX_SCANOUT_J313_POOL_SIZE);
    fake->free_calls++;
}

static bool fake_map(void *opaque, enum hv_agx_scanout_dart dart, uint64_t iova,
                     uint64_t pa, uint64_t size)
{
    struct fake_platform *fake = opaque;

    assert(dart < HV_AGX_SCANOUT_DART_COUNT);
    assert(iova == fake->iova + fake->mapped_bytes[dart]);
    assert(pa == fake->pa_base + fake->mapped_bytes[dart]);
    assert(size > 0 && size <= HV_AGX_SCANOUT_SERVICE_CHUNK_SIZE);
    fake->map_calls[dart]++;
    if ((int32_t)dart == fake->fail_map_dart &&
        fake->map_calls[dart] == fake->fail_map_call)
        return false;
    fake->mapped_bytes[dart] += size;
    return true;
}

static void fake_unmap(void *opaque, enum hv_agx_scanout_dart dart, uint64_t iova,
                       uint64_t size)
{
    struct fake_platform *fake = opaque;

    assert(dart < HV_AGX_SCANOUT_DART_COUNT);
    assert(size > 0 && size <= HV_AGX_SCANOUT_SERVICE_CHUNK_SIZE);
    assert(iova + size == fake->iova + fake->mapped_bytes[dart] -
                                  fake->unmapped_bytes[dart]);
    fake->unmap_calls[dart]++;
    fake->unmapped_bytes[dart] += size;
}

static bool fake_present_begin(void *opaque, uint64_t surface_iova,
                               const struct hv_agx_scanout_request *request,
                               uint64_t *cookie)
{
    struct fake_platform *fake = opaque;

    assert(request->Command == HV_AGX_SCANOUT_CMD_PRESENT);
    assert(fake->snapshot_calls == fake->present_begin_calls + 1);
    fake->present_begin_calls++;
    fake->presented_iova = surface_iova;
    *cookie = UINT64_C(0x1234);
    return true;
}

static void fake_diagnostic_snapshot(void *opaque, uint64_t surface_pa,
                                     uint64_t surface_iova,
                                     const struct hv_agx_scanout_request *request)
{
    struct fake_platform *fake = opaque;
    assert(request->Command == HV_AGX_SCANOUT_CMD_PRESENT);
    fake->snapshot_calls++;
    fake->snapshot_pa = surface_pa;
    fake->snapshot_iova = surface_iova;
}

static bool fake_diagnostic_fill(void *opaque, uint64_t surface_pa,
                                 uint64_t surface_iova,
                                 const struct hv_agx_scanout_request *request)
{
    struct fake_platform *fake = opaque;

    assert(request->Command == HV_AGX_SCANOUT_CMD_PRESENT);
    fake->diagnostic_calls++;
    fake->diagnostic_pa = surface_pa;
    fake->diagnostic_iova = surface_iova;
    return !fake->diagnostic_fail;
}

static enum hv_agx_scanout_async_result fake_present_poll(void *opaque,
                                                           uint64_t cookie,
                                                           uint32_t *swap_id)
{
    struct fake_platform *fake = opaque;

    assert(cookie == UINT64_C(0x1234));
    fake->present_poll_calls++;
    *swap_id = fake->applied_swap_id;
    return fake->present_result;
}

static enum hv_agx_scanout_latch_result fake_present_latch_poll(
    void *opaque, uint32_t expected_swap_id)
{
    struct fake_platform *fake = opaque;

    fake->latch_poll_calls++;
    fake->latch_expected_swap_id = expected_swap_id;
    return fake->latch_result;
}

static bool fake_quiesce_begin(void *opaque, uint64_t *cookie)
{
    struct fake_platform *fake = opaque;

    fake->quiesce_begin_calls++;
    *cookie = UINT64_C(0x5678);
    return fake->quiesce_begin_ok;
}

static enum hv_agx_scanout_async_result fake_quiesce_poll(void *opaque,
                                                           uint64_t cookie)
{
    struct fake_platform *fake = opaque;

    assert(cookie == UINT64_C(0x5678));
    fake->quiesce_poll_calls++;
    return fake->quiesce_result;
}

static const struct hv_agx_scanout_platform_ops ops = {
    .translate = fake_translate,
    .is_ram = fake_is_ram,
    .reserve_iova = fake_reserve_iova,
    .free_iova = fake_free_iova,
    .map = fake_map,
    .unmap = fake_unmap,
    .diagnostic_fill = fake_diagnostic_fill,
    .diagnostic_snapshot = fake_diagnostic_snapshot,
    .present_begin = fake_present_begin,
    .present_poll = fake_present_poll,
    .present_latch_poll = fake_present_latch_poll,
    .quiesce_begin = fake_quiesce_begin,
    .quiesce_poll = fake_quiesce_poll,
};

static void fake_init(struct fake_platform *fake)
{
    memset(fake, 0, sizeof(*fake));
    fake->ipa_base = UINT64_C(0x40000000);
    fake->pa_base = UINT64_C(0x880000000);
    fake->iova = UINT64_C(0x10000000);
    fake->unmapped_page = -1;
    fake->non_ram_page = -1;
    fake->noncontiguous_page = -1;
    fake->fail_map_dart = -1;
    fake->quiesce_begin_ok = true;
    fake->present_result = HV_AGX_SCANOUT_ASYNC_PENDING;
    fake->latch_result = HV_AGX_SCANOUT_LATCH_PENDING;
    fake->quiesce_result = HV_AGX_SCANOUT_ASYNC_PENDING;
}

static void write32(struct hv_agx_scanout_broker *broker, uint64_t reg, uint32_t value)
{
    uint64_t wire = value;
    assert(hv_agx_scanout_broker_mmio(broker, reg, &wire, true, 2));
}

static void write64(struct hv_agx_scanout_broker *broker, uint64_t reg, uint64_t value)
{
    assert(hv_agx_scanout_broker_mmio(broker, reg, &value, true, 3));
}

static void submit_register(struct hv_agx_scanout_broker *broker,
                            const struct fake_platform *fake, uint64_t sequence)
{
    write64(broker, HV_AGX_SCANOUT_REG_POOL_IPA, fake->ipa_base);
    write64(broker, HV_AGX_SCANOUT_REG_POOL_SIZE, HV_AGX_SCANOUT_J313_POOL_SIZE);
    write64(broker, HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, sequence);
    write32(broker, HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_REGISTER_POOL);
}

static void submit_present(struct hv_agx_scanout_broker *broker, uint64_t sequence,
                           uint64_t offset)
{
    write64(broker, HV_AGX_SCANOUT_REG_SURFACE_OFFSET, offset);
    write64(broker, HV_AGX_SCANOUT_REG_SURFACE_SIZE, HV_AGX_SCANOUT_J313_SURFACE_SIZE);
    write32(broker, HV_AGX_SCANOUT_REG_WIDTH, HV_AGX_SCANOUT_J313_WIDTH);
    write32(broker, HV_AGX_SCANOUT_REG_HEIGHT, HV_AGX_SCANOUT_J313_HEIGHT);
    write32(broker, HV_AGX_SCANOUT_REG_STRIDE, HV_AGX_SCANOUT_J313_STRIDE);
    write32(broker, HV_AGX_SCANOUT_REG_FORMAT, HV_AGX_SCANOUT_FORMAT_BGRA8888);
    write64(broker, HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, sequence);
    write32(broker, HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_PRESENT);
}

static enum hv_agx_scanout_service_step_result step(
    struct hv_agx_scanout_service *service, struct hv_agx_scanout_broker *broker,
    struct fake_platform *fake)
{
    enum hv_agx_scanout_service_step_result result;

    fake->translate_calls_this_step = 0;
    result = hv_agx_scanout_service_step(service, broker);
    if (fake->translate_calls_this_step > fake->max_translate_calls_per_step)
        fake->max_translate_calls_per_step = fake->translate_calls_this_step;
    return result;
}

static void drive_until_idle(struct hv_agx_scanout_service *service,
                             struct hv_agx_scanout_broker *broker,
                             struct fake_platform *fake)
{
    for (unsigned iteration = 0; iteration < 20000; iteration++) {
        step(service, broker, fake);
        if (service->state == HV_AGX_SCANOUT_SERVICE_IDLE)
            return;
    }
    assert(!"service did not become idle");
}

static void register_pool(struct hv_agx_scanout_service *service,
                          struct hv_agx_scanout_broker *broker,
                          struct fake_platform *fake)
{
    submit_register(broker, fake, 1);
    drive_until_idle(service, broker, fake);
    assert(broker->state == HV_AGX_SCANOUT_READY);
}

static void test_register_is_bounded_and_maps_exact_pool_into_both_darts(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_service service;
    struct fake_platform fake;

    fake_init(&fake);
    hv_agx_scanout_broker_init(&broker);
    hv_agx_scanout_service_init(&service, &ops, &fake);
    register_pool(&service, &broker, &fake);

    assert(fake.translate_calls == HV_AGX_SCANOUT_SERVICE_PAGE_COUNT);
    assert(fake.max_translate_calls_per_step <= HV_AGX_SCANOUT_SERVICE_PAGES_PER_STEP);
    assert(fake.reserve_calls == 1);
    assert(fake.mapped_bytes[HV_AGX_SCANOUT_DART_DISPLAY] == HV_AGX_SCANOUT_J313_POOL_SIZE);
    assert(fake.mapped_bytes[HV_AGX_SCANOUT_DART_DCP] == HV_AGX_SCANOUT_J313_POOL_SIZE);
    assert(service.owns_pool);
    assert(broker.pool_pa == fake.pa_base);
    assert(broker.pool_iova == fake.iova);
}

static void test_register_rejects_each_invalid_backing_class(void)
{
    const enum hv_agx_scanout_result expected[] = {
        HV_AGX_SCANOUT_RESULT_UNMAPPED,
        HV_AGX_SCANOUT_RESULT_NOT_RAM,
        HV_AGX_SCANOUT_RESULT_NONCONTIGUOUS,
    };

    for (unsigned kind = 0; kind < sizeof(expected) / sizeof(expected[0]); kind++) {
        struct hv_agx_scanout_broker broker;
        struct hv_agx_scanout_service service;
        struct fake_platform fake;

        fake_init(&fake);
        if (kind == 0)
            fake.unmapped_page = 37;
        else if (kind == 1)
            fake.non_ram_page = 37;
        else
            fake.noncontiguous_page = 37;
        hv_agx_scanout_broker_init(&broker);
        hv_agx_scanout_service_init(&service, &ops, &fake);
        submit_register(&broker, &fake, 1);
        drive_until_idle(&service, &broker, &fake);

        assert(broker.state == HV_AGX_SCANOUT_UNREGISTERED);
        assert(broker.result == expected[kind]);
        assert(fake.reserve_calls == 0);
        assert(!service.owns_pool);
    }
}

static void test_register_rejects_wrapping_physical_range_before_reservation(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_service service;
    struct fake_platform fake;

    fake_init(&fake);
    fake.pa_base = UINT64_MAX - (HV_AGX_SCANOUT_SERVICE_PAGE_SIZE - 1);
    hv_agx_scanout_broker_init(&broker);
    hv_agx_scanout_service_init(&service, &ops, &fake);
    submit_register(&broker, &fake, 1);
    drive_until_idle(&service, &broker, &fake);

    assert(broker.state == HV_AGX_SCANOUT_UNREGISTERED);
    assert(broker.result == HV_AGX_SCANOUT_RESULT_NONCONTIGUOUS);
    assert(fake.reserve_calls == 0);
}

static void test_second_dart_map_failure_rolls_back_and_releases_iova(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_service service;
    struct fake_platform fake;

    fake_init(&fake);
    fake.fail_map_dart = HV_AGX_SCANOUT_DART_DCP;
    fake.fail_map_call = 3;
    hv_agx_scanout_broker_init(&broker);
    hv_agx_scanout_service_init(&service, &ops, &fake);
    submit_register(&broker, &fake, 1);
    drive_until_idle(&service, &broker, &fake);

    assert(broker.state == HV_AGX_SCANOUT_UNREGISTERED);
    assert(broker.result == HV_AGX_SCANOUT_RESULT_MAP_FAILED);
    assert(fake.unmapped_bytes[HV_AGX_SCANOUT_DART_DISPLAY] ==
           fake.mapped_bytes[HV_AGX_SCANOUT_DART_DISPLAY]);
    assert(fake.unmapped_bytes[HV_AGX_SCANOUT_DART_DCP] ==
           fake.mapped_bytes[HV_AGX_SCANOUT_DART_DCP]);
    assert(fake.free_calls == 1);
    assert(!service.owns_pool);
}

static void test_present_completes_only_after_platform_reports_applied_swap(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_service service;
    struct fake_platform fake;
    const uint64_t offset = UINT64_C(0x10000);

    fake_init(&fake);
    hv_agx_scanout_broker_init(&broker);
    hv_agx_scanout_service_init(&service, &ops, &fake);
    register_pool(&service, &broker, &fake);
    submit_present(&broker, 2, offset);

    assert(step(&service, &broker, &fake) == HV_AGX_SCANOUT_SERVICE_PROGRESSED);
    assert(step(&service, &broker, &fake) == HV_AGX_SCANOUT_SERVICE_PROGRESSED);
    assert(fake.presented_iova == fake.iova + offset);
    assert(fake.diagnostic_calls == 1);
    assert(fake.diagnostic_pa == fake.pa_base + offset);
    assert(fake.diagnostic_iova == fake.iova + offset);
    assert(fake.snapshot_calls == 1);
    assert(fake.snapshot_pa == fake.pa_base + offset);
    assert(fake.snapshot_iova == fake.iova + offset);
    assert(broker.state == HV_AGX_SCANOUT_PENDING);
    assert(step(&service, &broker, &fake) == HV_AGX_SCANOUT_SERVICE_WAITING);
    assert(broker.applied_sequence == 1);

    fake.present_result = HV_AGX_SCANOUT_ASYNC_APPLIED;
    fake.applied_swap_id = 41;
    assert(step(&service, &broker, &fake) == HV_AGX_SCANOUT_SERVICE_PROGRESSED);
    assert(broker.state == HV_AGX_SCANOUT_ACTIVE);
    assert(broker.applied_sequence == 2);
    assert(broker.swap_id == 41);
    assert(fake.latch_poll_calls == 0);
}

static void test_diagnostic_fill_failure_blocks_swap(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_service service;
    struct fake_platform fake;

    fake_init(&fake);
    fake.diagnostic_fail = true;
    hv_agx_scanout_broker_init(&broker);
    hv_agx_scanout_service_init(&service, &ops, &fake);
    register_pool(&service, &broker, &fake);
    submit_present(&broker, 2, UINT64_C(0x30000));
    step(&service, &broker, &fake);
    step(&service, &broker, &fake);
    assert(fake.diagnostic_calls == 1);
    assert(fake.present_begin_calls == 0);
    assert(broker.result == HV_AGX_SCANOUT_RESULT_PRESENT_FAILED);
}

static void test_bgra_stripes_and_bounds(void)
{
    uint32_t pixels[16];

    memset(pixels, 0, sizeof(pixels));
    assert(hv_agx_scanout_fill_bgra_stripes(pixels, 8, 2, 32,
                                          sizeof(pixels)));
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 8; ++x) {
            const uint32_t expected[4] = {
                UINT32_C(0xffff0000), UINT32_C(0xff00ff00),
                UINT32_C(0xff0000ff), UINT32_C(0xffffffff),
            };
            assert(pixels[y * 8 + x] == expected[x / 2]);
        }
    assert(!hv_agx_scanout_fill_bgra_stripes(pixels, 8, 2, 31,
                                           sizeof(pixels)));
    assert(!hv_agx_scanout_fill_bgra_stripes(pixels, 8, 3, 32,
                                           sizeof(pixels)));
}

static void test_primary_snapshot_counts_channels_and_corners_without_writing(void)
{
    const uint32_t pixels[4] = {
        UINT32_C(0x00000000), UINT32_C(0xff112233),
        UINT32_C(0x800000ff), UINT32_C(0xffffffff),
    };
    struct hv_agx_scanout_pixel_stats stats;
    uint32_t copy[4];
    memcpy(copy, pixels, sizeof(copy));
    assert(hv_agx_scanout_pixel_stats(pixels, 2, 2, 8,
                                     sizeof(pixels), &stats));
    assert(stats.pixel_count == 4);
    assert(stats.nonzero_pixels == 3);
    assert(stats.channel_sum[0] == 0x33u + 0xffu + 0xffu);
    assert(stats.channel_sum[1] == 0x22u + 0xffu);
    assert(stats.channel_sum[2] == 0x11u + 0xffu);
    assert(stats.channel_sum[3] == 0xffu + 0x80u + 0xffu);
    assert(stats.corners[0] == pixels[0]);
    assert(stats.corners[1] == pixels[1]);
    assert(stats.corners[2] == pixels[2]);
    assert(stats.corners[3] == pixels[3]);
    assert(stats.hash != 0);
    assert(memcmp(copy, pixels, sizeof(copy)) == 0);
    assert(!hv_agx_scanout_pixel_stats(pixels, 2, 2, 7,
                                      sizeof(pixels), &stats));
}

static void test_v2_present_waits_for_exact_latch_after_applied(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_service service;
    struct fake_platform fake;

    fake_init(&fake);
    hv_agx_scanout_broker_init_v2(&broker, true);
    hv_agx_scanout_service_init(&service, &ops, &fake);
    register_pool(&service, &broker, &fake);
    submit_present(&broker, 2, UINT64_C(0x10000));
    step(&service, &broker, &fake);
    step(&service, &broker, &fake);
    fake.present_result = HV_AGX_SCANOUT_ASYNC_APPLIED;
    fake.applied_swap_id = 41;
    assert(step(&service, &broker, &fake) == HV_AGX_SCANOUT_SERVICE_PROGRESSED);
    assert(broker.applied_sequence == 2);
    assert(broker.latched_sequence == 0);
    assert(service.state == HV_AGX_SCANOUT_SERVICE_PRESENT_LATCH_POLL);

    assert(step(&service, &broker, &fake) == HV_AGX_SCANOUT_SERVICE_WAITING);
    assert(fake.latch_expected_swap_id == 41);
    assert(broker.latched_sequence == 0);

    fake.latch_result = HV_AGX_SCANOUT_LATCHED;
    assert(step(&service, &broker, &fake) == HV_AGX_SCANOUT_SERVICE_PROGRESSED);
    assert(service.state == HV_AGX_SCANOUT_SERVICE_IDLE);
    assert(broker.latched_sequence == 2);
    assert((broker.irq_status & HV_AGX_SCANOUT_IRQ_LATCHED) != 0);

    submit_present(&broker, 3, UINT64_C(0x20000));
    assert(step(&service, &broker, &fake) == HV_AGX_SCANOUT_SERVICE_PROGRESSED);
}

static void test_v2_wrong_swap_latch_fails_closed_without_retirement(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_service service;
    struct fake_platform fake;

    fake_init(&fake);
    hv_agx_scanout_broker_init_v2(&broker, true);
    hv_agx_scanout_service_init(&service, &ops, &fake);
    register_pool(&service, &broker, &fake);
    submit_present(&broker, 2, UINT64_C(0x10000));
    step(&service, &broker, &fake);
    step(&service, &broker, &fake);
    fake.present_result = HV_AGX_SCANOUT_ASYNC_APPLIED;
    fake.applied_swap_id = 41;
    step(&service, &broker, &fake);
    fake.latch_result = HV_AGX_SCANOUT_LATCH_WRONG_SWAP;
    assert(step(&service, &broker, &fake) == HV_AGX_SCANOUT_SERVICE_PROGRESSED);
    assert(service.state == HV_AGX_SCANOUT_SERVICE_IDLE);
    assert(broker.applied_sequence == 2);
    assert(broker.latched_sequence == 0);
    assert((broker.irq_status & HV_AGX_SCANOUT_IRQ_ERROR) != 0);
}

static void test_async_failures_preserve_registered_pool_ownership(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_service service;
    struct fake_platform fake;

    fake_init(&fake);
    hv_agx_scanout_broker_init(&broker);
    hv_agx_scanout_service_init(&service, &ops, &fake);
    register_pool(&service, &broker, &fake);

    submit_present(&broker, 2, 0);
    step(&service, &broker, &fake);
    step(&service, &broker, &fake);
    fake.present_result = HV_AGX_SCANOUT_ASYNC_FAILED;
    drive_until_idle(&service, &broker, &fake);
    assert(broker.state == HV_AGX_SCANOUT_READY);
    assert(broker.result == HV_AGX_SCANOUT_RESULT_PRESENT_FAILED);
    assert(service.owns_pool);
    assert(fake.free_calls == 0);

    write64(&broker, HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, 3);
    write32(&broker, HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_RELEASE);
    step(&service, &broker, &fake);
    step(&service, &broker, &fake);
    fake.quiesce_result = HV_AGX_SCANOUT_ASYNC_FAILED;
    drive_until_idle(&service, &broker, &fake);
    assert(broker.state == HV_AGX_SCANOUT_READY);
    assert(broker.result == HV_AGX_SCANOUT_RESULT_NOT_QUIESCED);
    assert(service.owns_pool);
    assert(fake.free_calls == 0);
    assert(fake.unmapped_bytes[0] == 0 && fake.unmapped_bytes[1] == 0);
}

static void test_release_preserves_ownership_until_quiesced_then_unmaps_bounded(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_service service;
    struct fake_platform fake;

    fake_init(&fake);
    hv_agx_scanout_broker_init(&broker);
    hv_agx_scanout_service_init(&service, &ops, &fake);
    register_pool(&service, &broker, &fake);

    fake.quiesce_begin_ok = false;
    write64(&broker, HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, 2);
    write32(&broker, HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_RELEASE);
    drive_until_idle(&service, &broker, &fake);
    assert(broker.state == HV_AGX_SCANOUT_READY);
    assert(broker.result == HV_AGX_SCANOUT_RESULT_NOT_QUIESCED);
    assert(service.owns_pool);
    assert(fake.free_calls == 0);
    assert(fake.unmapped_bytes[0] == 0 && fake.unmapped_bytes[1] == 0);

    fake.quiesce_begin_ok = true;
    write64(&broker, HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, 3);
    write32(&broker, HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_RELEASE);
    assert(step(&service, &broker, &fake) == HV_AGX_SCANOUT_SERVICE_PROGRESSED);
    assert(step(&service, &broker, &fake) == HV_AGX_SCANOUT_SERVICE_PROGRESSED);
    assert(step(&service, &broker, &fake) == HV_AGX_SCANOUT_SERVICE_WAITING);
    assert(service.owns_pool);
    assert(fake.unmapped_bytes[0] == 0 && fake.unmapped_bytes[1] == 0);

    fake.quiesce_result = HV_AGX_SCANOUT_ASYNC_APPLIED;
    drive_until_idle(&service, &broker, &fake);
    assert(broker.state == HV_AGX_SCANOUT_UNREGISTERED);
    assert(fake.unmapped_bytes[HV_AGX_SCANOUT_DART_DISPLAY] ==
           HV_AGX_SCANOUT_J313_POOL_SIZE);
    assert(fake.unmapped_bytes[HV_AGX_SCANOUT_DART_DCP] ==
           HV_AGX_SCANOUT_J313_POOL_SIZE);
    assert(fake.free_calls == 1);
    assert(!service.owns_pool);
}

int main(void)
{
    test_register_is_bounded_and_maps_exact_pool_into_both_darts();
    test_register_rejects_each_invalid_backing_class();
    test_register_rejects_wrapping_physical_range_before_reservation();
    test_second_dart_map_failure_rolls_back_and_releases_iova();
    test_present_completes_only_after_platform_reports_applied_swap();
    test_diagnostic_fill_failure_blocks_swap();
    test_bgra_stripes_and_bounds();
    test_primary_snapshot_counts_channels_and_corners_without_writing();
    test_v2_present_waits_for_exact_latch_after_applied();
    test_v2_wrong_swap_latch_fails_closed_without_retirement();
    test_async_failures_preserve_registered_pool_ownership();
    test_release_preserves_ownership_until_quiesced_then_unmaps_bounded();
    puts("hv_agx_scanout_service_test: ok");
    return 0;
}
