/* SPDX-License-Identifier: MIT */
#include "../src/hv_ans_memory.h"
#include "../src/hv_ans_prp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * A first-address-only or attr-stripped translator authorizes DMA across an
 * unmapped/protected byte, reverses direction, loses offsets, or accepts a
 * software hook as real RAM. All maps/addresses below are synthetic. */
#define ATTR UINT64_C(0x7fc)
#define RAM UINT64_C(0x880000000)
struct fake_leaf { uint64_t ipa, raw; uint8_t shift; };
struct fake_map { struct fake_leaf leaf[8]; size_t count, reads; };
static bool raw_leaf(void *ctx, uint64_t ipa, uint64_t *raw, uint8_t *shift)
{
    struct fake_map *m = ctx;
    ++m->reads;
    for (size_t i = 0; i < m->count; ++i) {
        uint64_t size = UINT64_C(1) << m->leaf[i].shift;
        if (ipa >= m->leaf[i].ipa && ipa - m->leaf[i].ipa < size) {
            *raw = m->leaf[i].raw; *shift = m->leaf[i].shift; return true;
        }
    }
    return false;
}
static struct hv_contract_snapshot snapshot;
static struct fake_map map;
static struct ans_memory_range private_pa[2];
static struct ans_memory_context c;
static void reset(void)
{
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.header.magic = HV_CONTRACT_MAGIC;
    snapshot.header.version = HV_CONTRACT_VERSION;
    snapshot.boot.ram_base = RAM;
    snapshot.boot.ram_size = 0x10000000;
    snapshot.region_count = HV_CONTRACT_REGION_DART_TABLES + 1;
    for (unsigned i = 0; i < snapshot.region_count; ++i) {
        snapshot.regions[i].kind = i;
        snapshot.regions[i].base = RAM + 0x100000 + i * 0x4000;
        snapshot.regions[i].size = 0x4000;
    }
    snapshot.regions[HV_CONTRACT_REGION_GUEST_RAM].base = RAM;
    snapshot.regions[HV_CONTRACT_REGION_GUEST_RAM].size = 0x10000000;
    snapshot.regions[HV_CONTRACT_REGION_LOW_MEMORY].base = RAM;
    snapshot.regions[HV_CONTRACT_REGION_LOW_MEMORY].size = 0x10000000;
    private_pa[0] = (struct ans_memory_range){RAM + 0x200000, 0x4000};
    private_pa[1] = (struct ans_memory_range){RAM + 0x210000, 0x4000};
    memset(&map, 0, sizeof(map));
    map.leaf[0] = (struct fake_leaf){0x100000, RAM | ATTR | 3, 14};
    map.leaf[1] = (struct fake_leaf){0x104000, (RAM + 0x4000) | ATTR | 3, 14};
    map.count = 2;
    c = (struct ans_memory_context){
        .launch = &snapshot, .private_pa = private_pa, .private_count = 2,
        .ramdisk = {RAM + 0x300000, 0x100000}, .reservations_complete = true,
        .ipa_bits = 36, .leaf_context = &map, .raw_leaf = raw_leaf
    };
}
static void denied(uint64_t ipa, size_t bytes, uint32_t dir)
{
    uint64_t pa = UINT64_MAX;
    assert(!ans_memory_translate(&c, ipa, bytes, dir, &pa));
    assert(pa == 0);
}
static uint64_t allowed(uint64_t ipa, size_t bytes, uint32_t dir)
{
    uint64_t pa = 0;
    assert(ans_memory_translate(&c, ipa, bytes, dir, &pa));
    return pa;
}
static void ranges(void)
{
    reset();
    for (uint64_t off = 0; off < 0x4000; ++off)
        assert(allowed(0x100000 + off, 1, ANS_DMA_FROM_DEVICE) == RAM + off);
    assert(allowed(0x103ffe, 4, ANS_DMA_TO_DEVICE) == RAM + 0x3ffe);
    assert(allowed(0x103800, 4096, ANS_DMA_TO_DEVICE) == RAM + 0x3800);
    map.count = 1;
    denied(0x103800, 4096, ANS_DMA_TO_DEVICE);
    denied(0x103fff, 2, ANS_DMA_FROM_DEVICE);
    assert(allowed(0x103fff, 1, ANS_DMA_TO_DEVICE) == RAM + 0x3fff);
    map.count = 2;
    map.leaf[1].raw += 0x4000;
    denied(0x103800, 4096, ANS_DMA_TO_DEVICE); /* non-contiguous host extent */
    reset();
    map.leaf[0] = (struct fake_leaf){UINT64_C(0x100000000), RAM | ATTR | 1, 25};
    map.count = 1;
    assert(allowed(UINT64_C(0x100003abc), 131072, ANS_DMA_FROM_DEVICE) == RAM + 0x3abc);
    denied(UINT64_C(0x100003abc), 131073, ANS_DMA_FROM_DEVICE);
    map.leaf[0].raw += 0x4000; /* malformed block alignment */
    denied(UINT64_C(0x100003abc), 4, ANS_DMA_TO_DEVICE);
    reset();
    denied(UINT64_MAX - 3, 4, ANS_DMA_TO_DEVICE);
    denied(0x100000, 0, ANS_DMA_TO_DEVICE);
    denied(UINT64_C(1) << 36, 4, ANS_DMA_TO_DEVICE);
    size_t old_reads = map.reads;
    denied((UINT64_C(1) << 36) - 2, 4, ANS_DMA_TO_DEVICE);
    assert(map.reads == old_reads);
    for (unsigned bits = 0; bits < 64; ++bits) {
        c.ipa_bits = (uint8_t)bits;
        if (bits < 32 || bits > 44) denied(0x100000, 4, ANS_DMA_TO_DEVICE);
    }
}
static void permissions(void)
{
    reset();
    for (unsigned perm = 0; perm < 4; ++perm) {
        map.leaf[0].raw = RAM | (ATTR & ~UINT64_C(0xc0)) | ((uint64_t)perm << 6) | 3;
        if (perm & 1) assert(allowed(0x100123, 4, ANS_DMA_TO_DEVICE) == RAM + 0x123);
        else denied(0x100123, 4, ANS_DMA_TO_DEVICE);
        if (perm & 2) assert(allowed(0x100123, 4, ANS_DMA_FROM_DEVICE) == RAM + 0x123);
        else denied(0x100123, 4, ANS_DMA_FROM_DEVICE);
    }
    reset();
    map.leaf[1].raw &= ~UINT64_C(0x80);
    denied(0x103fff, 2, ANS_DMA_FROM_DEVICE); /* later leaf permission matters */
    assert(allowed(0x103fff, 2, ANS_DMA_TO_DEVICE) == RAM + 0x3fff);
    reset();
    for (unsigned bit = 0; bit < 64; ++bit) {
        if ((bit >= 2 && bit <= 5) || bit == 8 || bit == 9 || bit == 10 || bit == 0) {
            map.leaf[0].raw = (RAM | ATTR | 3) & ~(UINT64_C(1) << bit);
            denied(0x100000, 4, ANS_DMA_TO_DEVICE);
        }
    }
    for (unsigned bits = 11; bits <= 13; ++bits) {
        map.leaf[0].raw = RAM | ATTR | 3 | (UINT64_C(1) << bits);
        denied(0x100000, 4, ANS_DMA_TO_DEVICE);
    }
    map.leaf[0].raw = RAM | ATTR | 2 | (UINT64_C(1) << 50); /* SW hook */
    denied(0x100000, 4, ANS_DMA_TO_DEVICE);
    map.leaf[0].raw = 0; denied(0x100000, 4, ANS_DMA_TO_DEVICE);
    map.leaf[0].raw = RAM | ATTR | 1; denied(0x100000, 4, ANS_DMA_TO_DEVICE);
    reset();
    map.leaf[0].shift = 2; denied(0x100000, 4, ANS_DMA_TO_DEVICE);
    denied(0x100000, 4, ANS_DMA_NONE);
    denied(0x100000, 4, ANS_DMA_FROM_DEVICE | ANS_DMA_TO_DEVICE);
}
static void ownership(void)
{
    for (unsigned kind = 1; kind <= HV_CONTRACT_REGION_DART_TABLES; ++kind) {
        if (kind == HV_CONTRACT_REGION_LOW_MEMORY) continue;
        reset();
        snapshot.regions[kind].base = RAM + 0x800;
        snapshot.regions[kind].size = 1; /* Interior byte cannot be skipped. */
        denied(0x100000, 4096, ANS_DMA_FROM_DEVICE);
        assert(allowed(0x100000, 0x800, ANS_DMA_FROM_DEVICE) == RAM);
        assert(allowed(0x100801, 4, ANS_DMA_TO_DEVICE) == RAM + 0x801);
    }
    reset(); private_pa[0] = (struct ans_memory_range){RAM + 0x200, 1};
    denied(0x100000, 4096, ANS_DMA_TO_DEVICE);
    reset(); c.ramdisk = (struct ans_memory_range){RAM + 0x200, 1};
    denied(0x100000, 4096, ANS_DMA_FROM_DEVICE);
    reset(); map.leaf[0].raw = (RAM + 0x200000) | ATTR | 3;
    denied(0x100000, 4, ANS_DMA_TO_DEVICE); /* alternate guest alias of private PA */
    reset(); map.leaf[0].raw = (RAM - 0x4000) | ATTR | 3;
    denied(0x100000, 4, ANS_DMA_TO_DEVICE);
    reset(); snapshot.boot.ram_size = 0x800;
    denied(0x100000, 4096, ANS_DMA_TO_DEVICE);
    reset(); snapshot.regions[0].size = 0x800;
    denied(0x100000, 4096, ANS_DMA_TO_DEVICE);
    reset(); snapshot.regions[0].size = 0;
    denied(0x100000, 4, ANS_DMA_TO_DEVICE); /* LOW_MEMORY alone grants nothing */
    reset(); snapshot.region_count--;
    denied(0x100000, 4, ANS_DMA_TO_DEVICE);
    reset(); snapshot.regions[7].kind = 1;
    denied(0x100000, 4, ANS_DMA_TO_DEVICE);
    reset(); snapshot.regions[7].kind = 99;
    denied(0x100000, 4, ANS_DMA_TO_DEVICE);
    reset(); snapshot.regions[7].base = UINT64_MAX;
    denied(0x100000, 4, ANS_DMA_TO_DEVICE);
    reset(); snapshot.boot.ram_base = UINT64_MAX;
    denied(0x100000, 4, ANS_DMA_TO_DEVICE);
    reset(); c.ramdisk = (struct ans_memory_range){UINT64_MAX, 1};
    denied(0x100000, 4, ANS_DMA_TO_DEVICE);
    reset(); private_pa[0].base = UINT64_MAX;
    denied(0x100000, 4, ANS_DMA_TO_DEVICE);
    reset(); c.private_pa = NULL;
    denied(0x100000, 4, ANS_DMA_TO_DEVICE);
    reset(); c.reservations_complete = false;
    denied(0x100000, 4, ANS_DMA_TO_DEVICE);
    reset(); snapshot.header.magic = 0;
    denied(0x100000, 4, ANS_DMA_TO_DEVICE);
}
static bool no_metadata(void *ctx, uint64_t pa, void *dst, size_t n)
{
    (void)ctx; (void)pa; (void)dst; (void)n;
    return false;
}
static void prp_callback(void)
{
    const struct ans_memory_ops ops = {ans_memory_translate, no_metadata};
    struct ans_prp_shadow shadow;
    reset();
    assert(ans_prp_translate(&ops, &c, 0x103800, 0x104000, 4096,
                            ANS_DMA_FROM_DEVICE, &shadow) == ANS_RESULT_OK);
    assert(shadow.page_count == 2 && shadow.host_pages[0] == RAM + 0x3800 &&
           shadow.host_pages[1] == RAM + 0x4000);
    map.leaf[1].raw &= ~UINT64_C(0x80);
    assert(ans_prp_translate(&ops, &c, 0x103800, 0x104000, 4096,
                            ANS_DMA_FROM_DEVICE, &shadow) == ANS_RESULT_DENIED);
    assert(ans_prp_translate(&ops, &c, 0x103800, 0x104000, 4096,
                            ANS_DMA_TO_DEVICE, &shadow) == ANS_RESULT_OK);
}
#ifdef ANS_TEST_STAGE2_SOURCE
/* The offline runner may extract the exact hv_stage2_get_leaf function and
 * its hv_vm.c macros into this header, to exercise the production accessor
 * without linking the rest of the exception/emulation/system-register code. */
#include ANS_TEST_STAGE2_SOURCE
#include <stdlib.h>
static void authoritative_accessor(void)
{
    u64 *top = aligned_alloc(0x4000, 0x4000);
    u64 *l2 = aligned_alloc(0x4000, 0x4000);
    u64 *l3 = aligned_alloc(0x4000, 0x4000);
    assert(top && l2 && l3);
    memset(top, 0, 0x4000); memset(l2, 0, 0x4000); memset(l3, 0, 0x4000);
    u64 raw; u8 shift;
    hv_Ltop = top; vaddr_bits = 36;
    top[0] = (u64)l3 | 3;
    l3[64] = RAM | ATTR | 3;
    assert(hv_stage2_get_leaf(0x100123, &raw, &shift));
    assert(raw == l3[64] && shift == 14 && (raw & 0xc0) == 0xc0);
    l3[64] &= ~UINT64_C(0x80);
    assert(hv_stage2_get_leaf(0x103fff, &raw, &shift) && !(raw & 0x80));
    l3[64] = (u64)l2 | 2; /* software sub-page table: must not dereference */
    assert(!hv_stage2_get_leaf(0x100123, &raw, &shift) && !raw && !shift);
    top[0] = (u64)l3 | 2; /* invalid parent table */
    assert(!hv_stage2_get_leaf(0x100123, &raw, &shift));
    top[0] = (u64)l3 | 3 | (UINT64_C(1) << 60);
    assert(!hv_stage2_get_leaf(0x100123, &raw, &shift));
    top[0] = RAM | ATTR | 1;
    assert(hv_stage2_get_leaf(0x100123, &raw, &shift) && raw == top[0] && shift == 25);
    assert(!hv_stage2_get_leaf(UINT64_C(1) << 36, &raw, &shift));
    vaddr_bits = 40;
    top[1] = (u64)l2 | 3;
    l2[0] = (u64)l3 | 3;
    l3[0] = RAM | ATTR | 3;
    assert(hv_stage2_get_leaf((UINT64_C(1) << 36) + 7, &raw, &shift));
    assert(raw == l3[0] && shift == 14);
    top[1] &= ~UINT64_C(1);
    assert(!hv_stage2_get_leaf(UINT64_C(1) << 36, &raw, &shift));
    hv_Ltop = NULL;
    assert(!hv_stage2_get_leaf(0, &raw, &shift));
    free(top); free(l2); free(l3);
    puts("ANS authoritative raw stage-2 accessor tests PASS");
}
#endif
/* Real bug caught: a guest PRP pointing into an iBoot carveout inside guest
 * RAM (ANSDART001-R3 found 13) would be DMA-authorized; or the kernel-range
 * descriptor entry would wrongly exclude all of guest RAM. Layout is J313's. */
static void carveouts(void)
{
    const struct ans_memory_range guest = {0x850000000, 0x18f708000};
    const struct ans_memory_range kernel = {0x802078000, 0x1dd690000};
    const struct ans_memory_range cv[] = {
        {0x802078000, 0x1dd690000},   /* region-id-24: exact kernel range, skip */
        {0x864204000, 0x7a00000},     /* region-id-167: inside guest */
        {0x9fff34000, 0x28000},       /* region-id-16: above guest */
        {0x0, 0x0},                   /* unused slot */
        {0x84fffc000, 0x8000},        /* straddles the guest base */
        {0x800000000, 0x4000},        /* below guest */
    };
    struct ans_memory_range out[4] = {{1, 1}};
    size_t n = 1;
    assert(ans_memory_carveout_exclusions(cv, 6, guest, kernel, out, 4, &n));
    assert(n == 3 && out[0].base == 1);
    assert(out[1].base == 0x864204000 && out[1].size == 0x7a00000);
    assert(out[2].base == 0x84fffc000);
    n = 3;
    assert(!ans_memory_carveout_exclusions(cv, 6, guest, kernel, out, 4, &n)); /* over cap */
    const struct ans_memory_range zero[] = {{0x860000000, 0}};
    n = 0;
    assert(!ans_memory_carveout_exclusions(zero, 1, guest, kernel, out, 4, &n));
    const struct ans_memory_range wrap[] = {{UINT64_MAX - 0x1000, 0x4000}};
    n = 0;
    assert(!ans_memory_carveout_exclusions(wrap, 1, guest, kernel, out, 4, &n));

    /* Translation denies a PRP into an excluded carveout page. */
    reset();
    struct ans_memory_range priv[3] = {private_pa[0], private_pa[1], {RAM + 0x4000, 0x4000}};
    c.private_pa = priv;
    c.private_count = 3;
    assert(allowed(0x100000, 4096, ANS_DMA_FROM_DEVICE) == RAM);
    denied(0x104000, 4096, ANS_DMA_FROM_DEVICE);
    denied(0x103800, 4096, ANS_DMA_TO_DEVICE);
}
int main(void)
{
    ranges(); permissions(); ownership(); prp_callback(); carveouts();
#ifdef ANS_TEST_STAGE2_SOURCE
    authoritative_accessor();
#endif
    puts("ANS full-range memory authorization tests PASS");
    return 0;
}
