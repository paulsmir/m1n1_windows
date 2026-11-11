/* SPDX-License-Identifier: MIT */
/*
 * S2 (broker ABI V3) regression guard: a fixed V1/V2 guest session replayed
 * against the broker and the service.  Every guest-visible MMIO read (all
 * offsets of the page window, both widths, handled flag and value) and every
 * platform call with its arguments is folded into FNV-1a digests.  The golden
 * digests were produced by this file compiled against the pre-V3 sources
 * (m1n1 f6176bba, -DREPLAY_PRINT_ONLY), so any V1/V2-visible change fails.
 *
 * Digest A covers the V1/V2 window (0x00..0xa7) and the platform calls;
 * digest B covers offsets 0xa8..0x1ff and V3 command codes, which a V1/V2
 * guest only ever sees refused.  With CAP_TWO_LAYERS advertised but not
 * negotiated, digest A (CAPABILITIES masked of that bit) must still match.
 *
 * This file uses only the V1/V2 API so that it builds against both trees.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/hv_agx_scanout_service.h"

#define POOL_IPA UINT64_C(0x10000000)
#define POOL_PA UINT64_C(0x880000000)
#define POOL_IOVA UINT64_C(0x40000000)
#define POOL_SIZE UINT64_C(0x2000000)

static uint64_t digest_a, digest_b;
static uint64_t *cur = &digest_a;
static bool snapshot_v3_window = true;
static uint32_t caps_mask = UINT32_MAX;
#ifdef HV_AGX_SCANOUT_CAP_TWO_LAYERS
static bool advertise_two_layers;
#endif

static void mix(uint64_t value)
{
    for (unsigned i = 0; i < 8; i++) {
        *cur ^= (value >> (8 * i)) & 0xff;
        *cur *= UINT64_C(0x100000001b3);
    }
}

struct fake {
    uint32_t swap_id;
    enum hv_agx_scanout_async_result present_result;
    enum hv_agx_scanout_latch_result latch_result;
    bool quiesce_ok;
};

static void mix_request(const struct hv_agx_scanout_request *r)
{
    /* The V1/V2 fields, one by one (the struct grew in V3). */
    mix(r->Command); mix(r->Width); mix(r->Height); mix(r->Stride);
    mix(r->Format); mix(r->Sequence); mix(r->PoolIpa); mix(r->PoolSize);
    mix(r->SurfaceOffset); mix(r->SurfaceSize); mix(r->DestX); mix(r->DestY);
    mix(r->DestWidth); mix(r->DestHeight);
}

static bool op_translate(void *o, uint64_t ipa, uint64_t *pa)
{
    (void)o;
    mix(0x101); mix(ipa);
    *pa = POOL_PA + (ipa - POOL_IPA);
    return true;
}
static bool op_is_ram(void *o, uint64_t pa, uint64_t size)
{
    (void)o;
    mix(0x102); mix(pa); mix(size);
    return true;
}
static bool op_reserve(void *o, uint64_t size, uint64_t alignment, uint64_t *iova)
{
    (void)o;
    mix(0x103); mix(size); mix(alignment);
    *iova = POOL_IOVA;
    return true;
}
static void op_free(void *o, uint64_t iova, uint64_t size)
{
    (void)o;
    mix(0x104); mix(iova); mix(size);
}
static bool op_map(void *o, enum hv_agx_scanout_dart dart, uint64_t iova,
                   uint64_t pa, uint64_t size)
{
    (void)o;
    mix(0x105); mix(dart); mix(iova); mix(pa); mix(size);
    return true;
}
static void op_unmap(void *o, enum hv_agx_scanout_dart dart, uint64_t iova,
                     uint64_t size)
{
    (void)o;
    mix(0x106); mix(dart); mix(iova); mix(size);
}
static bool op_present_begin(void *o, uint64_t surface_iova,
                             const struct hv_agx_scanout_request *request,
                             uint64_t *cookie)
{
    (void)o;
    mix(0x107); mix(surface_iova); mix_request(request);
    *cookie = 0x77;
    return true;
}
static enum hv_agx_scanout_async_result op_present_poll(void *o, uint64_t cookie,
                                                        uint32_t *swap_id)
{
    struct fake *f = o;
    mix(0x108); mix(cookie);
    *swap_id = ++f->swap_id;
    return f->present_result;
}
static enum hv_agx_scanout_latch_result op_latch_poll(void *o, uint32_t expected)
{
    struct fake *f = o;
    mix(0x109); mix(expected);
    return f->latch_result;
}
static bool op_quiesce_begin(void *o, uint64_t *cookie)
{
    (void)o;
    mix(0x10a);
    *cookie = 0x99;
    return true;
}
static enum hv_agx_scanout_async_result op_quiesce_poll(void *o, uint64_t cookie)
{
    struct fake *f = o;
    mix(0x10b); mix(cookie);
    return f->quiesce_ok ? HV_AGX_SCANOUT_ASYNC_APPLIED : HV_AGX_SCANOUT_ASYNC_FAILED;
}

static const struct hv_agx_scanout_platform_ops ops = {
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
};

static struct hv_agx_scanout_broker broker;
static struct hv_agx_scanout_service service;
static struct fake fake;

static void snap(void)
{
    uint64_t *saved = cur;

    for (uint64_t offset = 0; offset < 0x200; offset += 4) {
        for (unsigned width = 2; width <= 3; width++) {
            uint64_t value = 0;
            bool handled;

            if (width == 3 && (offset & 7))
                continue;
            if (offset >= HV_AGX_SCANOUT_MMIO_SIZE && !snapshot_v3_window)
                continue;
            cur = offset < HV_AGX_SCANOUT_MMIO_SIZE ? saved : &digest_b;
            handled = hv_agx_scanout_broker_mmio(&broker, offset, &value, false, width);
            if (handled && width == 2 && offset == HV_AGX_SCANOUT_REG_CAPABILITIES)
                value &= caps_mask;
            mix(offset); mix(width); mix(handled); mix(handled ? value : 0);
        }
    }
    cur = saved;
    mix(service.state);
}

static void w32(uint64_t reg, uint32_t value)
{
    uint64_t wire = value;
    mix(0x201); mix(reg); mix(value);
    mix(hv_agx_scanout_broker_mmio(&broker, reg, &wire, true, 2));
}
static void w64(uint64_t reg, uint64_t value)
{
    mix(0x202); mix(reg); mix(value);
    mix(hv_agx_scanout_broker_mmio(&broker, reg, &value, true, 3));
}

static void command(uint64_t sequence, uint32_t cmd)
{
    w64(HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, sequence);
    w32(HV_AGX_SCANOUT_REG_COMMAND, cmd);
    snap();
}

static void drive(void)
{
    for (unsigned i = 0; i < 100000; i++) {
        enum hv_agx_scanout_service_step_result r =
            hv_agx_scanout_service_step(&service, &broker);
        mix(0x300); mix(r);
        snap();
        if (r == HV_AGX_SCANOUT_SERVICE_IDLE_STEP)
            return;
    }
    assert(!"service did not settle");
}

static void stage_surface(uint64_t offset, uint32_t width, uint32_t height,
                          uint32_t stride, uint32_t format, uint32_t dest_origin,
                          uint32_t dest_size)
{
    w64(HV_AGX_SCANOUT_REG_SURFACE_OFFSET, offset);
    w64(HV_AGX_SCANOUT_REG_SURFACE_SIZE, (uint64_t)stride * height);
    w32(HV_AGX_SCANOUT_REG_WIDTH, width);
    w32(HV_AGX_SCANOUT_REG_HEIGHT, height);
    w32(HV_AGX_SCANOUT_REG_STRIDE, stride);
    w32(HV_AGX_SCANOUT_REG_FORMAT, format);
    w32(HV_AGX_SCANOUT_REG_DEST_ORIGIN, dest_origin);
    w32(HV_AGX_SCANOUT_REG_DEST_SIZE, dest_size);
}

static void native(uint64_t offset, uint32_t format)
{
    stage_surface(offset, 2560, 1600, 10240, format, 0, 2560u | (1600u << 16));
}

static void owner_session(uint64_t *seq)
{
    w64(HV_AGX_SCANOUT_REG_POOL_IPA, POOL_IPA + 0x1000);
    w64(HV_AGX_SCANOUT_REG_POOL_SIZE, POOL_SIZE);
    command(++*seq, HV_AGX_SCANOUT_CMD_REGISTER_POOL); /* unaligned: refused */
    w64(HV_AGX_SCANOUT_REG_POOL_IPA, POOL_IPA);
    command(++*seq, HV_AGX_SCANOUT_CMD_REGISTER_POOL);
    drive();
    w32(HV_AGX_SCANOUT_REG_IRQ_ENABLE, HV_AGX_SCANOUT_IRQ_MASK);
    native(0, HV_AGX_SCANOUT_FORMAT_BGRA8888);
    command(++*seq, HV_AGX_SCANOUT_CMD_PRESENT);
    drive();
    mix(hv_agx_scanout_broker_take_irq_edge(&broker));
    w32(HV_AGX_SCANOUT_REG_IRQ_STATUS, HV_AGX_SCANOUT_IRQ_MASK);
    snap();
    /* bad stride, stale sequence */
    stage_surface(0, 2560, 1600, 10176, HV_AGX_SCANOUT_FORMAT_BGRA8888, 0,
                  2560u | (1600u << 16));
    command(++*seq, HV_AGX_SCANOUT_CMD_PRESENT);
    command(*seq - 1, HV_AGX_SCANOUT_CMD_PRESENT);
    /* scaled source (V2 only), RGBA, outside the pool */
    stage_surface(0x1000000, 1920, 1080, 7680, HV_AGX_SCANOUT_FORMAT_BGRA8888,
                  0u | (80u << 16), 2560u | (1440u << 16));
    command(++*seq, HV_AGX_SCANOUT_CMD_PRESENT);
    drive();
    native(0x1000000, HV_AGX_SCANOUT_FORMAT_RGBA8888);
    command(++*seq, HV_AGX_SCANOUT_CMD_PRESENT);
    drive();
    native(0x1100000, HV_AGX_SCANOUT_FORMAT_BGRA8888);
    command(++*seq, HV_AGX_SCANOUT_CMD_PRESENT);
    drive();
    /* busy: a second request before the service takes the first */
    native(0, HV_AGX_SCANOUT_FORMAT_BGRA8888);
    command(++*seq, HV_AGX_SCANOUT_CMD_PRESENT);
    command(++*seq, HV_AGX_SCANOUT_CMD_PRESENT);
    drive();
    /* a present the platform fails, then one that latches */
    fake.present_result = HV_AGX_SCANOUT_ASYNC_FAILED;
    command(++*seq, HV_AGX_SCANOUT_CMD_PRESENT);
    drive();
    fake.present_result = HV_AGX_SCANOUT_ASYNC_APPLIED;
    command(++*seq, HV_AGX_SCANOUT_CMD_PRESENT);
    drive();
    /* a release the platform cannot prove, then the real one */
    fake.quiesce_ok = false;
    command(++*seq, HV_AGX_SCANOUT_CMD_RELEASE);
    drive();
    fake.quiesce_ok = true;
    command(++*seq, HV_AGX_SCANOUT_CMD_RELEASE);
    drive();
}

static void replay(int mode)
{
    uint64_t seq = 0;

    cur = &digest_a;
    memset(&fake, 0, sizeof(fake));
    fake.present_result = HV_AGX_SCANOUT_ASYNC_APPLIED;
    fake.latch_result = HV_AGX_SCANOUT_LATCHED;
    fake.quiesce_ok = true;
    memset(&broker, 0xa5, sizeof(broker));
    if (mode == 0)
        hv_agx_scanout_broker_init(&broker);
    else
        hv_agx_scanout_broker_init_v2(&broker, mode == 1);
#ifdef HV_AGX_SCANOUT_CAP_TWO_LAYERS
    if (advertise_two_layers)
        assert(hv_agx_scanout_broker_enable_two_layers(&broker));
#endif
    assert(hv_agx_scanout_broker_set_pool_size(&broker, POOL_SIZE));
    hv_agx_scanout_service_init(&service, &ops, &fake);
    snap();
    command(++seq, HV_AGX_SCANOUT_CMD_QUERY);
    owner_session(&seq);
    owner_session(&seq); /* a new owner after RELEASE */
    /* Codes and offsets V3 adds: refused for a V1/V2 guest. */
    if (snapshot_v3_window) {
        cur = &digest_b;
        for (uint64_t reg = HV_AGX_SCANOUT_MMIO_SIZE; reg < 0x200; reg += 4) {
            uint64_t value = 0x2000;
            mix(hv_agx_scanout_broker_mmio(&broker, reg, &value, true, 2));
            if (!(reg & 7))
                mix(hv_agx_scanout_broker_mmio(&broker, reg, &value, true, 3));
        }
        command(++seq, 4);
        command(++seq, 5);
        cur = &digest_a;
    }
}

int main(void)
{
    uint64_t a[3], b[3];

    for (int mode = 0; mode < 3; mode++) {
        digest_a = digest_b = UINT64_C(0xcbf29ce484222325);
        replay(mode);
        a[mode] = digest_a;
        b[mode] = digest_b;
        printf("legacy replay mode %d: A=0x%016llx B=0x%016llx\n", mode,
               (unsigned long long)a[mode], (unsigned long long)b[mode]);
    }
#ifndef REPLAY_PRINT_ONLY
    /* Golden values: this file against f6176bba (pre-V3) sources. */
    static const uint64_t golden_a[3] = {
        UINT64_C(0x747053ee3c5596fe), UINT64_C(0x892a1ba6780ce5be),
        UINT64_C(0xe806d22775dfae9a)};
    static const uint64_t golden_b[3] = {
        UINT64_C(0xa282d3f0e06f70e2), UINT64_C(0x62f4c557cdcb24aa),
        UINT64_C(0x52c37a86575d11a2)};
    for (int mode = 0; mode < 3; mode++) {
        assert(a[mode] == golden_a[mode]);
        assert(b[mode] == golden_b[mode]);
    }
#ifdef HV_AGX_SCANOUT_CAP_TWO_LAYERS
    /* A V2 broker advertising CAP_TWO_LAYERS, owner never negotiates: the
     * V1/V2 window and every platform call are unchanged. */
    snapshot_v3_window = false;
    advertise_two_layers = true;
    caps_mask = ~HV_AGX_SCANOUT_CAP_TWO_LAYERS;
    digest_a = digest_b = UINT64_C(0xcbf29ce484222325);
    replay(1);
    printf("legacy replay mode 1 + CAP_TWO_LAYERS advertised: A=0x%016llx\n",
           (unsigned long long)digest_a);
    assert(digest_a == golden_a[1]);
#endif
#endif
    puts("hv_agx_scanout_legacy_replay_test: ok");
    return 0;
}
