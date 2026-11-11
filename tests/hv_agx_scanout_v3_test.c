/* SPDX-License-Identifier: MIT */
/*
 * S2: scanout broker ABI V3 (two DCP layers per commit), host side.
 *
 * WHAT REAL BUG THESE TESTS CATCH: a stale top layer (DCP slot 1) left on
 * screen after the guest stops using it, across one-layer commits, RELEASE,
 * a new owner and a reset; a guest-controlled surface escaping the scanout
 * pool or overlapping the other layer; a V3 commit whose DCP swap differs
 * from the two-slot builder's record for the same inputs.  V1/V2 identity is
 * hv_agx_scanout_legacy_replay_test.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/hv_agx_scanout_layers.h"
#include "../src/hv_agx_scanout_service.h"

#define POOL_IPA UINT64_C(0x10000000)
#define POOL_PA UINT64_C(0x880000000)
#define POOL_IOVA UINT64_C(0x40000000)
#define POOL_SIZE UINT64_C(0x4000000)
#define TOP_OFFSET UINT64_C(0x1000000)
#define FULL_PANEL (2560u | (1600u << 16))
#define PACK(lo, hi) HV_AGX_SCANOUT_DEST_PACK(lo, hi)

static uint32_t u32at(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t u64at(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

/* ------------------------------------------------------------------ fakes */

struct fake {
    uint32_t swap_id;
    enum hv_agx_scanout_async_result present_result;
    enum hv_agx_scanout_latch_result latch_result;
    enum hv_agx_scanout_async_result quiesce_result;
    uint32_t present_calls, layers_calls, poll_calls, latch_calls;
    uint32_t quiesce_calls, quiesce_slot1_calls;
    uint32_t removal_swaps; /* swaps whose record removes slot 1 */
    struct dcp_iomfb_present_request record;
    struct hv_agx_scanout_request last;
    uint64_t last_pool_iova;
};

static bool op_translate(void *o, uint64_t ipa, uint64_t *pa)
{
    (void)o;
    *pa = POOL_PA + (ipa - POOL_IPA);
    return true;
}
static bool op_is_ram(void *o, uint64_t pa, uint64_t size)
{
    (void)o; (void)pa; (void)size;
    return true;
}
static bool op_reserve(void *o, uint64_t size, uint64_t alignment, uint64_t *iova)
{
    (void)o; (void)size; (void)alignment;
    *iova = POOL_IOVA;
    return true;
}
static void op_free(void *o, uint64_t iova, uint64_t size)
{
    (void)o; (void)iova; (void)size;
}
static bool op_map(void *o, enum hv_agx_scanout_dart dart, uint64_t iova,
                   uint64_t pa, uint64_t size)
{
    (void)o; (void)dart; (void)iova; (void)pa; (void)size;
    return true;
}
static void op_unmap(void *o, enum hv_agx_scanout_dart dart, uint64_t iova,
                     uint64_t size)
{
    (void)o; (void)dart; (void)iova; (void)size;
}
static bool op_present_begin(void *o, uint64_t surface_iova,
                             const struct hv_agx_scanout_request *request,
                             uint64_t *cookie)
{
    struct fake *f = o;
    (void)surface_iova;
    assert(request->Command == HV_AGX_SCANOUT_CMD_PRESENT);
    assert(request->LayerCount == 0 && !request->RemoveSlot1);
    f->present_calls++;
    *cookie = 1;
    return true;
}
/* The hypervisor glue: request -> DCP layers -> the swap record. */
static bool op_present_layers_begin(void *o, uint64_t pool_iova,
                                    const struct hv_agx_scanout_request *request,
                                    uint64_t *cookie)
{
    struct fake *f = o;
    struct dcp_iomfb_present_layer layers[DCP_IOMFB_J313_LAYERS];
    uint32_t remove_mask = 0;

    f->layers_calls++;
    f->last = *request;
    f->last_pool_iova = pool_iova;
    if (!hv_agx_scanout_request_to_dcp_layers(request, pool_iova, layers,
                                              &remove_mask) ||
        !dcp_iomfb_present_build_slots_v13_5(&f->record, layers, false,
                                             remove_mask))
        return false;
    if (u32at(f->record.bytes + 0x104) & 2u && f->record.bytes[0x1878] == 1)
        f->removal_swaps++;
    *cookie = 2;
    return true;
}
static enum hv_agx_scanout_async_result op_present_poll(void *o, uint64_t cookie,
                                                        uint32_t *swap_id)
{
    struct fake *f = o;
    (void)cookie;
    f->poll_calls++;
    *swap_id = ++f->swap_id;
    return f->present_result;
}
static enum hv_agx_scanout_latch_result op_latch_poll(void *o, uint32_t expected)
{
    struct fake *f = o;
    assert(expected == f->swap_id);
    f->latch_calls++;
    return f->latch_result;
}
static bool op_quiesce_begin(void *o, uint64_t *cookie)
{
    struct fake *f = o;
    f->quiesce_calls++;
    *cookie = 3;
    return true;
}
static bool op_quiesce_slot1_begin(void *o, uint64_t *cookie)
{
    struct fake *f = o;
    f->quiesce_slot1_calls++;
    f->removal_swaps++;
    *cookie = 4;
    return true;
}
static enum hv_agx_scanout_async_result op_quiesce_poll(void *o, uint64_t cookie)
{
    struct fake *f = o;
    (void)cookie;
    return f->quiesce_result;
}

static struct hv_agx_scanout_platform_ops ops = {
    .translate = op_translate,
    .is_ram = op_is_ram,
    .reserve_iova = op_reserve,
    .free_iova = op_free,
    .map = op_map,
    .unmap = op_unmap,
    .present_begin = op_present_begin,
    .present_poll = op_present_poll,
    .present_latch_poll = op_latch_poll,
    .quiesce_begin = op_quiesce_begin,
    .quiesce_poll = op_quiesce_poll,
    .present_layers_begin = op_present_layers_begin,
    .quiesce_slot1_begin = op_quiesce_slot1_begin,
};

static struct hv_agx_scanout_broker broker;
static struct hv_agx_scanout_service service;
static struct fake fake;
static uint64_t seq;

/* --------------------------------------------------------------- helpers */

static bool mmio(uint64_t reg, uint64_t *value, bool write, unsigned width)
{
    return hv_agx_scanout_broker_mmio(&broker, reg, value, write, width);
}
static void w32(uint64_t reg, uint32_t value)
{
    uint64_t wire = value;
    assert(mmio(reg, &wire, true, 2));
}
static void w64(uint64_t reg, uint64_t value)
{
    assert(mmio(reg, &value, true, 3));
}
static uint32_t r32(uint64_t reg)
{
    uint64_t value = 0;
    assert(mmio(reg, &value, false, 2));
    return (uint32_t)value;
}
static uint64_t r64(uint64_t reg)
{
    uint64_t value = 0;
    assert(mmio(reg, &value, false, 3));
    return value;
}

static void drive(void)
{
    for (unsigned i = 0; i < 100000; i++)
        if (hv_agx_scanout_service_step(&service, &broker) ==
            HV_AGX_SCANOUT_SERVICE_IDLE_STEP)
            return;
    assert(!"service did not settle");
}

static void command(uint32_t cmd)
{
    w64(HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, ++seq);
    w32(HV_AGX_SCANOUT_REG_COMMAND, cmd);
}

struct layer_spec {
    uint64_t offset, size;
    uint32_t width, height, stride, format;
    uint32_t src_origin, src_size, dst_origin, dst_size;
    uint32_t flags, reserved0;
    uint64_t reserved1;
};

static void stage_layer(unsigned index, const struct layer_spec *l)
{
    uint64_t base = index ? HV_AGX_SCANOUT_REG_LAYER1 : HV_AGX_SCANOUT_REG_LAYER0;

    w64(base + HV_AGX_SCANOUT_LAYER_SURFACE_OFFSET, l->offset);
    w64(base + HV_AGX_SCANOUT_LAYER_SURFACE_SIZE, l->size);
    w32(base + HV_AGX_SCANOUT_LAYER_WIDTH, l->width);
    w32(base + HV_AGX_SCANOUT_LAYER_HEIGHT, l->height);
    w32(base + HV_AGX_SCANOUT_LAYER_STRIDE, l->stride);
    w32(base + HV_AGX_SCANOUT_LAYER_FORMAT, l->format);
    w32(base + HV_AGX_SCANOUT_LAYER_SRC_ORIGIN, l->src_origin);
    w32(base + HV_AGX_SCANOUT_LAYER_SRC_SIZE, l->src_size);
    w32(base + HV_AGX_SCANOUT_LAYER_DST_ORIGIN, l->dst_origin);
    w32(base + HV_AGX_SCANOUT_LAYER_DST_SIZE, l->dst_size);
    w32(base + HV_AGX_SCANOUT_LAYER_FLAGS, l->flags);
    w32(base + HV_AGX_SCANOUT_LAYER_RESERVED0, l->reserved0);
    w64(base + HV_AGX_SCANOUT_LAYER_RESERVED1, l->reserved1);
}

/* The two-slot builder test's inputs (tests/dcp_iomfb_present_test.c): a
 * native BGRA desktop in slot 0 and a 1280x720 premultiplied RGBA overlay
 * at 640,440 in slot 1. */
static struct layer_spec bottom(void)
{
    return (struct layer_spec){
        .offset = 0, .size = 10240u * 1600u, .width = 2560, .height = 1600,
        .stride = 10240, .format = HV_AGX_SCANOUT_FORMAT_BGRA8888,
        .src_origin = 0, .src_size = FULL_PANEL, .dst_origin = 0,
        .dst_size = FULL_PANEL};
}
static struct layer_spec top(void)
{
    return (struct layer_spec){
        .offset = TOP_OFFSET, .size = 5120u * 720u, .width = 1280, .height = 720,
        .stride = 5120, .format = HV_AGX_SCANOUT_FORMAT_RGBA8888,
        .src_origin = 0, .src_size = PACK(1280, 720),
        .dst_origin = PACK(640, 440), .dst_size = PACK(1280, 720),
        .flags = HV_AGX_SCANOUT_LAYER_FLAG_PREMULTIPLIED};
}

static void commit(unsigned count, const struct layer_spec *l0,
                   const struct layer_spec *l1)
{
    stage_layer(0, l0);
    if (l1)
        stage_layer(1, l1);
    w32(HV_AGX_SCANOUT_REG_LAYER_COUNT, count);
    command(HV_AGX_SCANOUT_CMD_COMMIT_LAYERS);
}

static void negotiate(void)
{
    w32(HV_AGX_SCANOUT_REG_NEGOTIATED, HV_AGX_SCANOUT_CAP_TWO_LAYERS);
    assert(r32(HV_AGX_SCANOUT_REG_NEGOTIATED) == HV_AGX_SCANOUT_CAP_TWO_LAYERS);
}

static void register_pool(void)
{
    w64(HV_AGX_SCANOUT_REG_POOL_IPA, POOL_IPA);
    w64(HV_AGX_SCANOUT_REG_POOL_SIZE, POOL_SIZE);
    command(HV_AGX_SCANOUT_CMD_REGISTER_POOL);
    drive();
    assert(r32(HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_READY);
}

static void reset_fake(void)
{
    memset(&fake, 0, sizeof(fake));
    fake.present_result = HV_AGX_SCANOUT_ASYNC_APPLIED;
    fake.latch_result = HV_AGX_SCANOUT_LATCHED;
    fake.quiesce_result = HV_AGX_SCANOUT_ASYNC_APPLIED;
}

/* Fresh m1n1 broker (= a guest reset: PSCI SYSTEM_RESET reboots m1n1). */
static void boot(bool advertise)
{
    memset(&broker, 0xa5, sizeof(broker));
    hv_agx_scanout_broker_init_v2(&broker, true);
    if (advertise)
        assert(hv_agx_scanout_broker_enable_two_layers(&broker));
    assert(hv_agx_scanout_broker_set_pool_size(&broker, POOL_SIZE));
    hv_agx_scanout_service_init(&service, &ops, &fake);
    reset_fake();
}

static void owner_ready(void)
{
    boot(true);
    negotiate();
    register_pool();
}

static uint32_t active_layers(void)
{
    return r32(HV_AGX_SCANOUT_REG_ACTIVE_LAYERS);
}

/* ----------------------------------------------------------------- tests */

static void test_capability_and_negotiation(void)
{
    uint64_t value = 0;

    /* Not built in: the V3 window is not decoded, the bit is not set. */
    boot(false);
    assert(!(r32(HV_AGX_SCANOUT_REG_CAPABILITIES) & HV_AGX_SCANOUT_CAP_TWO_LAYERS));
    assert(r32(HV_AGX_SCANOUT_REG_ABI_VERSION) == HV_AGX_SCANOUT_ABI_VERSION_V2);
    for (uint64_t reg = HV_AGX_SCANOUT_MMIO_SIZE; reg < HV_AGX_SCANOUT_V3_MMIO_SIZE;
         reg += 4) {
        value = HV_AGX_SCANOUT_CAP_TWO_LAYERS;
        assert(!mmio(reg, &value, true, 2) && !mmio(reg, &value, false, 2));
    }
    command(HV_AGX_SCANOUT_CMD_COMMIT_LAYERS);
    assert(r32(HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_INVALID_COMMAND);

    /* Only a V2 broker with the latch source may advertise it. */
    hv_agx_scanout_broker_init(&broker);
    assert(!hv_agx_scanout_broker_enable_two_layers(&broker));
    hv_agx_scanout_broker_init_v2(&broker, false);
    assert(!hv_agx_scanout_broker_enable_two_layers(&broker));

    boot(true);
    assert(r32(HV_AGX_SCANOUT_REG_CAPABILITIES) & HV_AGX_SCANOUT_CAP_TWO_LAYERS);
    assert(r32(HV_AGX_SCANOUT_REG_ABI_VERSION) == HV_AGX_SCANOUT_ABI_VERSION_V2);
    assert(r32(HV_AGX_SCANOUT_REG_NEGOTIATED) == 0);
    assert(active_layers() == 0 && r64(HV_AGX_SCANOUT_REG_ACTIVE_OFFSET_1) == 0);
    /* unknown bits and non-optional bits are not taken */
    w32(HV_AGX_SCANOUT_REG_NEGOTIATED, HV_AGX_SCANOUT_CAP_TWO_LAYERS | 1u);
    assert(r32(HV_AGX_SCANOUT_REG_NEGOTIATED) == 0);
    w32(HV_AGX_SCANOUT_REG_NEGOTIATED, 1u << 10);
    assert(r32(HV_AGX_SCANOUT_REG_NEGOTIATED) == 0);
    /* read-only registers, the hole at 0x134, misaligned / wrong width */
    value = 1;
    assert(!mmio(HV_AGX_SCANOUT_REG_ACTIVE_LAYERS, &value, true, 2));
    assert(!mmio(HV_AGX_SCANOUT_REG_ACTIVE_OFFSET_1, &value, true, 3));
    assert(!mmio(0x134, &value, false, 2));
    assert(!mmio(HV_AGX_SCANOUT_REG_LAYER0 + 2, &value, false, 2));
    assert(!mmio(HV_AGX_SCANOUT_REG_LAYER0, &value, false, 2));
    assert(!mmio(HV_AGX_SCANOUT_REG_LAYER0 + HV_AGX_SCANOUT_LAYER_WIDTH, &value,
                 false, 3));
    assert(!mmio(HV_AGX_SCANOUT_V3_MMIO_SIZE, &value, false, 2));
    /* registered pool: negotiation is closed for this owner */
    register_pool();
    w32(HV_AGX_SCANOUT_REG_NEGOTIATED, HV_AGX_SCANOUT_CAP_TWO_LAYERS);
    assert(r32(HV_AGX_SCANOUT_REG_NEGOTIATED) == 0);
    /* staged records read back */
    struct layer_spec l = top();
    stage_layer(1, &l);
    assert(r64(HV_AGX_SCANOUT_REG_LAYER1 + HV_AGX_SCANOUT_LAYER_SURFACE_OFFSET) ==
           TOP_OFFSET);
    assert(r32(HV_AGX_SCANOUT_REG_LAYER1 + HV_AGX_SCANOUT_LAYER_DST_ORIGIN) ==
           PACK(640, 440));
}

struct refusal {
    const char *name;
    unsigned count;
    unsigned layer;   /* record the mutation applies to */
    int field;        /* which field to change */
    uint64_t value;
    enum hv_agx_scanout_result expected;
};

enum { F_NONE, F_OFFSET, F_SIZE, F_WIDTH, F_HEIGHT, F_STRIDE, F_FORMAT,
       F_SRC_ORIGIN, F_SRC_SIZE, F_DST_ORIGIN, F_DST_SIZE, F_FLAGS, F_RES0,
       F_RES1 };

static void apply(struct layer_spec *l, int field, uint64_t value)
{
    switch (field) {
    case F_OFFSET: l->offset = value; break;
    case F_SIZE: l->size = value; break;
    case F_WIDTH: l->width = (uint32_t)value; break;
    case F_HEIGHT: l->height = (uint32_t)value; break;
    case F_STRIDE: l->stride = (uint32_t)value; break;
    case F_FORMAT: l->format = (uint32_t)value; break;
    case F_SRC_ORIGIN: l->src_origin = (uint32_t)value; break;
    case F_SRC_SIZE: l->src_size = (uint32_t)value; break;
    case F_DST_ORIGIN: l->dst_origin = (uint32_t)value; break;
    case F_DST_SIZE: l->dst_size = (uint32_t)value; break;
    case F_FLAGS: l->flags = (uint32_t)value; break;
    case F_RES0: l->reserved0 = (uint32_t)value; break;
    case F_RES1: l->reserved1 = value; break;
    default: break;
    }
}

static void test_refusal_table(void)
{
    static const struct refusal table[] = {
        {"count 0", 0, 0, F_NONE, 0, HV_AGX_SCANOUT_RESULT_LAYER_COUNT},
        {"count 3", 3, 0, F_NONE, 0, HV_AGX_SCANOUT_RESULT_LAYER_COUNT},
        {"reserved0", 2, 1, F_RES0, 1, HV_AGX_SCANOUT_RESULT_LAYER_RESERVED},
        {"reserved1", 2, 0, F_RES1, 1ull << 63, HV_AGX_SCANOUT_RESULT_LAYER_RESERVED},
        {"flag bit 1", 2, 1, F_FLAGS, 3, HV_AGX_SCANOUT_RESULT_LAYER_RESERVED},
        {"format 0", 2, 0, F_FORMAT, 0, HV_AGX_SCANOUT_RESULT_LAYER_FORMAT},
        {"format 3", 2, 1, F_FORMAT, 3, HV_AGX_SCANOUT_RESULT_LAYER_FORMAT},
        {"width 0", 2, 1, F_WIDTH, 0, HV_AGX_SCANOUT_RESULT_LAYER_GEOMETRY},
        {"width > panel", 2, 0, F_WIDTH, 2561, HV_AGX_SCANOUT_RESULT_LAYER_GEOMETRY},
        {"height > panel", 2, 1, F_HEIGHT, 1601, HV_AGX_SCANOUT_RESULT_LAYER_GEOMETRY},
        {"size != stride*h", 2, 1, F_SIZE, 5120u * 720u + 0x10000,
         HV_AGX_SCANOUT_RESULT_LAYER_GEOMETRY},
        {"stride & 63", 2, 1, F_STRIDE, 5124, HV_AGX_SCANOUT_RESULT_LAYER_STRIDE},
        {"stride < w*4", 2, 0, F_STRIDE, 10176, HV_AGX_SCANOUT_RESULT_LAYER_STRIDE},
        {"offset unaligned", 2, 1, F_OFFSET, TOP_OFFSET + 0x4000,
         HV_AGX_SCANOUT_RESULT_LAYER_OUTSIDE_POOL},
        {"past pool end", 2, 1, F_OFFSET, POOL_SIZE - 0x10000,
         HV_AGX_SCANOUT_RESULT_LAYER_OUTSIDE_POOL},
        {"offset wraps", 2, 0, F_OFFSET, UINT64_C(0xffffffffffff0000),
         HV_AGX_SCANOUT_RESULT_LAYER_OUTSIDE_POOL},
        {"source empty", 2, 1, F_SRC_SIZE, PACK(0, 720),
         HV_AGX_SCANOUT_RESULT_LAYER_SOURCE_RECT},
        {"source past surface", 2, 1, F_SRC_ORIGIN, PACK(1, 0),
         HV_AGX_SCANOUT_RESULT_LAYER_SOURCE_RECT},
        {"dest 31 wide", 2, 1, F_DST_SIZE, PACK(31, 720),
         HV_AGX_SCANOUT_RESULT_LAYER_DEST_RECT},
        {"dest off panel", 2, 1, F_DST_ORIGIN, PACK(1281, 440),
         HV_AGX_SCANOUT_RESULT_LAYER_DEST_RECT},
        {"dest too tall", 2, 0, F_DST_SIZE, PACK(2560, 1601),
         HV_AGX_SCANOUT_RESULT_LAYER_DEST_RECT},
        {"scale > 2x down", 2, 1, F_DST_SIZE, PACK(639, 720),
         HV_AGX_SCANOUT_RESULT_LAYER_SCALE},
        {"scale > 2x up", 2, 1, F_SRC_SIZE, PACK(1280, 359),
         HV_AGX_SCANOUT_RESULT_LAYER_SCALE},
        {"same surface", 2, 1, F_OFFSET, 0, HV_AGX_SCANOUT_RESULT_LAYER_SAME_SURFACE},
        {"overlap", 2, 1, F_OFFSET, 0xf90000, HV_AGX_SCANOUT_RESULT_LAYER_OVERLAP},
    };
    bool seen[32] = {false};

    owner_ready();
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        struct layer_spec l[2] = {bottom(), top()};
        uint64_t rejected = r64(HV_AGX_SCANOUT_REG_REJECTED_REQUESTS);
        uint32_t calls = fake.layers_calls;

        apply(&l[table[i].layer], table[i].field, table[i].value);
        commit(table[i].count, &l[0], &l[1]);
        if (r32(HV_AGX_SCANOUT_REG_RESULT) != (uint32_t)table[i].expected)
            printf("refusal '%s': result %u expected %u\n", table[i].name,
                   r32(HV_AGX_SCANOUT_REG_RESULT), table[i].expected);
        assert(r32(HV_AGX_SCANOUT_REG_RESULT) == (uint32_t)table[i].expected);
        /* terminal receipt, nothing queued, nothing reached DCP */
        assert(r64(HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE) == seq);
        assert(r64(HV_AGX_SCANOUT_REG_REJECTED_REQUESTS) == rejected + 1);
        assert(r32(HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_READY);
        drive();
        assert(fake.layers_calls == calls);
        seen[table[i].expected] = true;
    }
    /* overlap from the other side: record 0 ends inside record 1 */
    {
        struct layer_spec l[2] = {bottom(), top()};
        l[1].offset = 0x10000;
        commit(2, &l[0], &l[1]);
        assert(r32(HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_LAYER_OVERLAP);
        /* adjacent ranges are fine */
        l[1].offset = 0xfa0000;
        commit(2, &l[0], &l[1]);
        assert(r32(HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_OK);
        drive();
        assert(fake.layers_calls == 1);
    }
    /* not negotiated: a new boot, pool registered without negotiation */
    boot(true);
    register_pool();
    {
        struct layer_spec l[2] = {bottom(), top()};
        commit(2, &l[0], &l[1]);
        assert(r32(HV_AGX_SCANOUT_REG_RESULT) ==
               HV_AGX_SCANOUT_RESULT_LAYERS_NOT_NEGOTIATED);
        assert(r64(HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE) == seq);
        seen[HV_AGX_SCANOUT_RESULT_LAYERS_NOT_NEGOTIATED] = true;
    }
    /* before REGISTER_POOL: the V2 rule for PRESENT */
    boot(true);
    negotiate();
    {
        struct layer_spec l[2] = {bottom(), top()};
        commit(2, &l[0], &l[1]);
        assert(r32(HV_AGX_SCANOUT_REG_RESULT) ==
               HV_AGX_SCANOUT_RESULT_INVALID_SURFACE);
    }
    /* SLOT1_LATCHED is covered by the state machine test. */
    seen[HV_AGX_SCANOUT_RESULT_SLOT1_LATCHED] = true;
    for (unsigned code = HV_AGX_SCANOUT_RESULT_LAYERS_NOT_NEGOTIATED;
         code <= HV_AGX_SCANOUT_RESULT_SLOT1_LATCHED; code++)
        assert(seen[code]);
}

static void expect_latched_commit(uint64_t offset0, uint64_t offset1)
{
    assert(r32(HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_ACTIVE);
    assert(r32(HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_OK);
    assert(r64(HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE) == seq);
    assert(r64(HV_AGX_SCANOUT_REG_APPLIED_SEQUENCE) == seq);
    assert(r64(HV_AGX_SCANOUT_REG_LATCHED_SEQUENCE) == seq);
    assert(r32(HV_AGX_SCANOUT_REG_SWAP_ID) == fake.swap_id);
    assert(r64(HV_AGX_SCANOUT_REG_ACTIVE_OFFSET) == offset0);
    assert(r64(HV_AGX_SCANOUT_REG_ACTIVE_OFFSET_1) == offset1);
}

/* A V3 two-layer commit is exactly the two-slot builder's record for the
 * same inputs, one swap, one latch, one IRQ. */
static void test_two_layer_commit_is_the_builder_record(void)
{
    struct dcp_iomfb_present_layer l[2];
    struct dcp_iomfb_present_request expected;
    struct layer_spec spec[2] = {bottom(), top()};

    owner_ready();
    w32(HV_AGX_SCANOUT_REG_IRQ_ENABLE, HV_AGX_SCANOUT_IRQ_MASK);
    commit(2, &spec[0], &spec[1]);
    assert(r32(HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_PENDING);
    drive();
    assert(fake.layers_calls == 1 && fake.poll_calls == 1 && fake.latch_calls == 1);
    assert(fake.present_calls == 0 && fake.last_pool_iova == POOL_IOVA);
    expect_latched_commit(0, TOP_OFFSET);
    assert(active_layers() == (HV_AGX_SCANOUT_ACTIVE_SLOT0 | HV_AGX_SCANOUT_ACTIVE_SLOT1));
    assert(r32(HV_AGX_SCANOUT_REG_IRQ_STATUS) == HV_AGX_SCANOUT_IRQ_LATCHED);
    assert(hv_agx_scanout_broker_take_irq_edge(&broker));
    assert(!hv_agx_scanout_broker_take_irq_edge(&broker));

    memset(l, 0, sizeof(l));
    l[0] = (struct dcp_iomfb_present_layer){
        .iova = POOL_IOVA, .width = 2560, .height = 1600, .stride = 10240,
        .dcp_format = DCP_IOMFB_FORMAT_BGRA, .source = {0, 0, 2560, 1600},
        .destination = {0, 0, 2560, 1600}, .surface_id = 3};
    l[1] = (struct dcp_iomfb_present_layer){
        .iova = POOL_IOVA + TOP_OFFSET, .width = 1280, .height = 720,
        .stride = 5120, .dcp_format = DCP_IOMFB_FORMAT_RGBA,
        .source = {0, 0, 1280, 720}, .destination = {640, 440, 1280, 720},
        .surface_id = 4, .premultiplied = true};
    assert(dcp_iomfb_present_build_layers_v13_5(&expected, l, false));
    assert(memcmp(expected.bytes, fake.record.bytes, sizeof(expected.bytes)) == 0);
    /* the slot-1 words the builder test checks */
    assert(u32at(fake.record.bytes + 0x54) == 3 && u32at(fake.record.bytes + 0x58) == 4);
    assert(u32at(fake.record.bytes + 0xd4) == 640 && u32at(fake.record.bytes + 0xd8) == 440);
    assert(u32at(fake.record.bytes + 0x104) == 3 && u32at(fake.record.bytes + 0x108) == 3);
    assert(fake.record.bytes[0x694 + 2] == 1);
    assert(u32at(fake.record.bytes + 0x694 + 0x0b) == DCP_IOMFB_FORMAT_RGBA);
    assert(u64at(fake.record.bytes + 0xd18) == POOL_IOVA);
    assert(u64at(fake.record.bytes + 0xd20) == POOL_IOVA + TOP_OFFSET);
    assert(fake.record.bytes[0x1877] == 0 && fake.record.bytes[0x1878] == 0);

    /* a cropped source with straight alpha reaches the record unchanged */
    spec[1].src_origin = PACK(16, 8);
    spec[1].src_size = PACK(640, 360);
    spec[1].flags = 0;
    commit(2, &spec[0], &spec[1]);
    drive();
    assert(u32at(fake.record.bytes + 0x74) == 16 && u32at(fake.record.bytes + 0x78) == 8);
    assert(u32at(fake.record.bytes + 0x7c) == 640 && u32at(fake.record.bytes + 0x80) == 360);
    assert(fake.record.bytes[0x694 + 2] == 0);
}

static void test_slot1_state_machine(void)
{
    struct layer_spec spec[2] = {bottom(), top()};
    struct dcp_iomfb_present_request one_slot;

    /* two -> one -> one -> two */
    owner_ready();
    commit(2, &spec[0], &spec[1]);
    drive();
    assert(fake.removal_swaps == 0);
    commit(1, &spec[0], NULL);
    assert(broker.pending.RemoveSlot1);
    drive();
    assert(fake.layers_calls == 2 && fake.removal_swaps == 1);
    assert(fake.last.RemoveSlot1 && fake.last.LayerCount == 1);
    assert(u32at(fake.record.bytes + 0x104) == 3 && fake.record.bytes[0x1878] == 1);
    expect_latched_commit(0, 0);
    assert(active_layers() == HV_AGX_SCANOUT_ACTIVE_SLOT0);
    commit(1, &spec[0], NULL);
    drive();
    assert(fake.layers_calls == 3 && fake.removal_swaps == 1);
    assert(!fake.last.RemoveSlot1);
    /* a one-layer commit without removal is the proven one-slot swap */
    assert(dcp_iomfb_present_build_v13_5(&one_slot, POOL_IOVA, 2560, 1600, 10240,
                                         DCP_IOMFB_FORMAT_BGRA, false, NULL));
    assert(memcmp(one_slot.bytes, fake.record.bytes, sizeof(one_slot.bytes)) == 0);
    commit(2, &spec[0], &spec[1]);
    drive();
    assert(active_layers() == (HV_AGX_SCANOUT_ACTIVE_SLOT0 | HV_AGX_SCANOUT_ACTIVE_SLOT1));

    /* A V2 PRESENT now would leave slot 1 on screen: refused. */
    w64(HV_AGX_SCANOUT_REG_SURFACE_OFFSET, 0);
    w64(HV_AGX_SCANOUT_REG_SURFACE_SIZE, HV_AGX_SCANOUT_J313_SURFACE_SIZE);
    w32(HV_AGX_SCANOUT_REG_WIDTH, 2560);
    w32(HV_AGX_SCANOUT_REG_HEIGHT, 1600);
    w32(HV_AGX_SCANOUT_REG_STRIDE, 10240);
    w32(HV_AGX_SCANOUT_REG_FORMAT, HV_AGX_SCANOUT_FORMAT_BGRA8888);
    command(HV_AGX_SCANOUT_CMD_PRESENT);
    assert(r32(HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_SLOT1_LATCHED);
    assert(r64(HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE) == seq);
    drive();
    assert(fake.present_calls == 0);

    /* release with slot 1 latched: the quiesce swap removes slot 1 */
    command(HV_AGX_SCANOUT_CMD_RELEASE);
    assert(broker.pending.RemoveSlot1);
    drive();
    assert(fake.quiesce_slot1_calls == 1 && fake.quiesce_calls == 0);
    assert(r32(HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_UNREGISTERED);
    assert(active_layers() == 0 && r64(HV_AGX_SCANOUT_REG_ACTIVE_OFFSET_1) == 0);
    assert(r32(HV_AGX_SCANOUT_REG_NEGOTIATED) == 0);

    /* new owner: nothing of slot 1 survives; it must negotiate again; its
     * V2 PRESENT is the plain V2 path */
    register_pool();
    commit(2, &spec[0], &spec[1]);
    assert(r32(HV_AGX_SCANOUT_REG_RESULT) ==
           HV_AGX_SCANOUT_RESULT_LAYERS_NOT_NEGOTIATED);
    command(HV_AGX_SCANOUT_CMD_PRESENT);
    assert(r32(HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_OK);
    drive();
    assert(fake.present_calls == 1);
    command(HV_AGX_SCANOUT_CMD_RELEASE);
    assert(!broker.pending.RemoveSlot1);
    drive();
    assert(fake.quiesce_calls == 1 && fake.quiesce_slot1_calls == 1);

    /* new owner with slot 1 latched and a failed RELEASE: slot 1 stays
     * tracked and the retried RELEASE removes it */
    negotiate();
    register_pool();
    commit(2, &spec[0], &spec[1]);
    drive();
    fake.quiesce_result = HV_AGX_SCANOUT_ASYNC_FAILED;
    command(HV_AGX_SCANOUT_CMD_RELEASE);
    drive();
    assert(r32(HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_NOT_QUIESCED);
    assert(r32(HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_ACTIVE);
    assert(active_layers() & HV_AGX_SCANOUT_ACTIVE_SLOT1);
    assert(fake.quiesce_slot1_calls == 2);
    fake.quiesce_result = HV_AGX_SCANOUT_ASYNC_APPLIED;
    command(HV_AGX_SCANOUT_CMD_RELEASE);
    drive();
    assert(fake.quiesce_slot1_calls == 3 && fake.quiesce_calls == 1);
    assert(active_layers() == 0);

    /* reset with slot 1 latched: a fresh broker starts with no slot 1, no
     * negotiation and no staged records (m1n1 reboots on SYSTEM_RESET; the
     * first owner swap clears the boot surfaces including slot 1) */
    owner_ready();
    commit(2, &spec[0], &spec[1]);
    drive();
    assert(active_layers() & HV_AGX_SCANOUT_ACTIVE_SLOT1);
    boot(true);
    assert(active_layers() == 0 && r32(HV_AGX_SCANOUT_REG_NEGOTIATED) == 0);
    assert(r32(HV_AGX_SCANOUT_REG_LAYER_COUNT) == 0);
    assert(r64(HV_AGX_SCANOUT_REG_LAYER1 + HV_AGX_SCANOUT_LAYER_SURFACE_OFFSET) == 0);
    assert(!broker.slot1_latched);
}

/* Failure paths keep slot 1 tracked: a stale top layer is worse than one
 * extra removal swap. */
static void test_slot1_failure_paths(void)
{
    struct layer_spec spec[2] = {bottom(), top()};

    /* a two-layer swap the platform failed may still have reached DCP */
    owner_ready();
    commit(1, &spec[0], NULL);
    drive();
    fake.present_result = HV_AGX_SCANOUT_ASYNC_FAILED;
    commit(2, &spec[0], &spec[1]);
    drive();
    assert(r32(HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_PRESENT_FAILED);
    assert(r32(HV_AGX_SCANOUT_REG_IRQ_STATUS) & HV_AGX_SCANOUT_IRQ_ERROR);
    assert(active_layers() & HV_AGX_SCANOUT_ACTIVE_SLOT1);
    fake.present_result = HV_AGX_SCANOUT_ASYNC_APPLIED;
    commit(1, &spec[0], NULL);
    assert(broker.pending.RemoveSlot1);
    drive();
    assert(fake.removal_swaps == 1 && active_layers() == HV_AGX_SCANOUT_ACTIVE_SLOT0);

    /* a removal swap whose latch fails: slot 1 stays tracked, the broker
     * stays busy for presents, RELEASE carries the removal */
    commit(2, &spec[0], &spec[1]);
    drive();
    fake.latch_result = HV_AGX_SCANOUT_LATCH_FAILED;
    commit(1, &spec[0], NULL);
    drive();
    assert(active_layers() & HV_AGX_SCANOUT_ACTIVE_SLOT1);
    commit(1, &spec[0], NULL);
    assert(r32(HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_BUSY);
    command(HV_AGX_SCANOUT_CMD_RELEASE);
    assert(broker.pending.RemoveSlot1);
    drive();
    assert(fake.quiesce_slot1_calls == 1 && active_layers() == 0);

    /* platform without the V3 ops: fail closed */
    ops.present_layers_begin = NULL;
    owner_ready();
    commit(2, &spec[0], &spec[1]);
    drive();
    assert(r32(HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_PRESENT_FAILED);
    ops.present_layers_begin = op_present_layers_begin;
    commit(2, &spec[0], &spec[1]);
    drive();
    ops.quiesce_slot1_begin = NULL;
    command(HV_AGX_SCANOUT_CMD_RELEASE);
    drive();
    assert(r32(HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_NOT_QUIESCED);
    assert(fake.quiesce_calls == 0);
    ops.quiesce_slot1_begin = op_quiesce_slot1_begin;
}

/* The translation itself refuses what the broker never produces. */
static void test_translation_guards(void)
{
    struct hv_agx_scanout_request request;
    struct dcp_iomfb_present_layer layers[2];
    uint32_t mask = 7;

    memset(&request, 0, sizeof(request));
    request.Command = HV_AGX_SCANOUT_CMD_COMMIT_LAYERS;
    request.LayerCount = 1;
    request.Layers[0].Format = HV_AGX_SCANOUT_FORMAT_BGRA8888;
    assert(hv_agx_scanout_request_to_dcp_layers(&request, POOL_IOVA, layers, &mask));
    assert(mask == 0 && layers[1].iova == 0 && layers[0].surface_id == 3);
    request.RemoveSlot1 = true;
    assert(hv_agx_scanout_request_to_dcp_layers(&request, POOL_IOVA, layers, &mask));
    assert(mask == 2);
    request.LayerCount = 2;
    assert(!hv_agx_scanout_request_to_dcp_layers(&request, POOL_IOVA, layers, &mask));
    request.RemoveSlot1 = false;
    request.Layers[1].Format = 0;
    assert(!hv_agx_scanout_request_to_dcp_layers(&request, POOL_IOVA, layers, &mask));
    request.Layers[1].Format = HV_AGX_SCANOUT_FORMAT_RGBA8888;
    request.Layers[1].SurfaceOffset = UINT64_MAX;
    assert(!hv_agx_scanout_request_to_dcp_layers(&request, POOL_IOVA, layers, &mask));
    request.Layers[1].SurfaceOffset = 0;
    request.Command = HV_AGX_SCANOUT_CMD_PRESENT;
    assert(!hv_agx_scanout_request_to_dcp_layers(&request, POOL_IOVA, layers, &mask));
}

int main(void)
{
    test_capability_and_negotiation();
    test_refusal_table();
    test_two_layer_commit_is_the_builder_record();
    test_slot1_state_machine();
    test_slot1_failure_paths();
    test_translation_guards();
    puts("hv_agx_scanout_v3_test: ok");
    return 0;
}
