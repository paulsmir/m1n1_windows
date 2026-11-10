/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../src/dcp_iomfb_present.h"
static uint32_t u32at(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t u64at(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static unsigned count_u32(const uint8_t *p, size_t n, uint32_t value)
{
    unsigned found = 0;
    for (size_t i = 0; i + 4 <= n; ++i)
        found += u32at(p + i) == value;
    return found;
}
/* FNV-1a 64 of a whole swap record. */
static uint64_t record_digest(const struct dcp_iomfb_present_request *request)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < sizeof(request->bytes); ++i) {
        hash ^= request->bytes[i];
        hash *= 0x100000001b3ull;
    }
    return hash;
}
/* H1: the one-slot records the scanout broker sends today, taken from the
 * builder of 14c893b2 (bench line).  The two-slot builder must not move a
 * byte of them. */
static void test_one_slot_records_are_the_bench_records(void)
{
    struct dcp_iomfb_present_request r;
    const struct dcp_iomfb_present_rect scaled = {0, 80, 2560, 1440};

    assert(sizeof(r.bytes) == 6276);
    assert(dcp_iomfb_present_build_v13_5(&r, 0x12340000, 2560, 1600, 10240, DCP_IOMFB_FORMAT_BGRA, false, NULL));
    assert(record_digest(&r) == 0x392cae8b8b469a1full);
    assert(dcp_iomfb_present_build_v13_5(&r, 0x12340000, 2560, 1600, 10240, DCP_IOMFB_FORMAT_BGRA, true, NULL));
    assert(record_digest(&r) == 0x64a2587d3e249dffull);
    assert(dcp_iomfb_present_build_v13_5(&r, 0x56780000, 1920, 1080, 7680, DCP_IOMFB_FORMAT_BGRA, false, &scaled));
    assert(record_digest(&r) == 0x73b039c595136986ull);
    assert(dcp_iomfb_present_build_v13_5(&r, 0x12340000, 2560, 1600, 10240, DCP_IOMFB_FORMAT_RGBA, false, NULL));
    assert(record_digest(&r) == 0x443e8a06dbd76e1full);
}
int main(void)
{
    struct dcp_iomfb_present_request req;
    uint8_t start_input[0x18];
    uint8_t start[0x18] = {0}, submit[0xc] = {0};
    uint32_t swap_id = 0;
    assert(!dcp_iomfb_present_build_v13_5(&req, 0x100000, 2560, 1600, 10176, DCP_IOMFB_FORMAT_BGRA, true, NULL));
    assert(!dcp_iomfb_present_build_v13_5(&req, 0x100000, UINT32_MAX, 2, UINT32_MAX, DCP_IOMFB_FORMAT_BGRA, true, NULL));
    assert(dcp_iomfb_present_build_v13_5(&req, 0x12340000, 2560, 1600, 10240, DCP_IOMFB_FORMAT_BGRA, true, NULL));
    assert(u64at(req.bytes + 0x40) == 0x861202);
    assert(u64at(req.bytes + 0x48) == 4);
    assert(u32at(req.bytes + 0x54) == 3);
    assert(u32at(req.bytes + 0xa4) == 1);
    assert(u32at(req.bytes + 0x104) == 0x80000007u);
    assert(u32at(req.bytes + 0x108) == 0x80000007u);
    assert(u32at(req.bytes + 0x10c) == 0);
    assert(u64at(req.bytes + 0x2e7) == 1);
    assert(u32at(req.bytes + 0x2ef) == 0x58f058d0);
    assert(req.bytes[0x2f3] == 0x40);
    assert(u32at(req.bytes + 0x64 + 8) == 2560);
    assert(u32at(req.bytes + 0x64 + 12) == 1600);
    assert(req.bytes[0x468 + 0] == 0);
    assert(u32at(req.bytes + 0x468 + 3) == 0);
    assert(u32at(req.bytes + 0x468 + 7) == 0);
    assert(req.bytes[0x468 + 0x13] == 13);
    assert(req.bytes[0x468 + 0x14] == 1);
    assert(u32at(req.bytes + 0x468 + 0x15) == 10240);
    assert(req.bytes[0x468 + 0x19] == 4);
    assert(u32at(req.bytes + 0x468 + 0x35) == 3);
    assert(u64at(req.bytes + 0x468 + 0x51) == 1);
    assert(u64at(req.bytes + 0x468 + 0x149) == 1);
    assert(u64at(req.bytes + 0xd18) == 0x12340000);
    assert(req.bytes[0x1876] == 0 && req.bytes[0x1877] == 0);
    assert(req.bytes[0x1878] == 1 && req.bytes[0x187b] == 1);
    assert(req.bytes[0x1880] == 0 && req.bytes[0x1881] == 1);
    assert(req.bytes[0x1882] == 1 && req.bytes[0x1883] == 0);
    assert(dcp_iomfb_present_build_start_v13_5(start_input,
                                                sizeof(start_input)));
    assert(u32at(start_input) == 0);
    assert(u64at(start_input + 4) == 0xfffffe1667ba4a00ull);
    assert(u32at(start_input + 12) == 0);
    assert(start_input[16] == 0 && start_input[17] == 1);
    for (unsigned int i = 18; i < sizeof(start_input); ++i)
        assert(start_input[i] == 0);
    assert(dcp_iomfb_present_build_v13_5(&req, 0x12340000, 2560, 1600, 10240, DCP_IOMFB_FORMAT_BGRA, false, NULL));
    assert(u32at(req.bytes + 0x104) == 1 && u32at(req.bytes + 0x108) == 1);
    assert(u32at(req.bytes + 0x10c) == 0);
    /* D3D-FS1: the surface fourcc follows the requested format (Asahi
     * iomfb_plane.h DCP_FORMAT_BGRA 'ARGB', DCP_FORMAT_RGBA 'ABGR'). */
    assert(count_u32(req.bytes, sizeof(req.bytes), 0x42475241u) == 1);
    assert(count_u32(req.bytes, sizeof(req.bytes), 0x52474241u) == 0);
    assert(dcp_iomfb_present_build_v13_5(&req, 0x12340000, 2560, 1600, 10240, DCP_IOMFB_FORMAT_RGBA, false, NULL));
    assert(count_u32(req.bytes, sizeof(req.bytes), 0x52474241u) == 1);
    assert(count_u32(req.bytes, sizeof(req.bytes), 0x42475241u) == 0);
    assert(!dcp_iomfb_present_build_v13_5(&req, 0x12340000, 2560, 1600, 10240, 0x30313272u, false, NULL));
    assert(dcp_iomfb_present_build_v13_5(&req, 0x12340000, 2560, 1600, 10240, DCP_IOMFB_FORMAT_BGRA, false, NULL));
    assert(dcp_iomfb_present_set_swap_id_v13_5(&req, 0x42));
    assert(u32at(req.bytes + 0x50) == 0x42);
    memcpy(start, &(uint32_t){0x42}, 4);
    assert(dcp_iomfb_present_parse_start_v13_5(start, sizeof(start), &swap_id));
    assert(swap_id == 0x42);
    memcpy(start + 0x14, &(uint32_t){1}, 4);
    assert(!dcp_iomfb_present_parse_start_v13_5(start, sizeof(start), &swap_id));
    assert(dcp_iomfb_present_parse_submit_v13_5(submit, sizeof(submit)));
    memcpy(submit + 5, &(uint32_t){1}, 4);
    assert(!dcp_iomfb_present_parse_submit_v13_5(submit, sizeof(submit)));
    /* rc106: destination rectangle (swap destination[0] at 0xc4) and black
     * background for a scaled source; native traffic is unchanged. */
    {
        struct dcp_iomfb_present_request native, explicit_native, scaled;
        struct dcp_iomfb_present_rect full = {0, 0, 2560, 1600};
        struct dcp_iomfb_present_rect fhd = {0, 80, 2560, 1440};
        struct dcp_iomfb_present_rect centered = {640, 440, 1280, 720};
        struct dcp_iomfb_present_rect too_tall = {0, 80, 2560, 1521};
        struct dcp_iomfb_present_rect too_wide = {1, 0, 2560, 1600};
        struct dcp_iomfb_present_rect empty = {0, 0, 0, 1600};
        struct dcp_iomfb_present_rect wrapped = {0xfffff000u, 0, 0x2000u, 1600};
        assert(dcp_iomfb_present_build_v13_5(&native, 0x12340000, 2560, 1600, 10240, DCP_IOMFB_FORMAT_BGRA, false, NULL));
        assert(dcp_iomfb_present_build_v13_5(&explicit_native, 0x12340000, 2560, 1600, 10240, DCP_IOMFB_FORMAT_BGRA, false, &full));
        assert(memcmp(native.bytes, explicit_native.bytes, sizeof(native.bytes)) == 0);
        assert(dcp_iomfb_present_build_v13_5(&native, 0x12340000, 2560, 1600, 10240, DCP_IOMFB_FORMAT_BGRA, true, NULL));
        assert(dcp_iomfb_present_build_v13_5(&explicit_native, 0x12340000, 2560, 1600, 10240, DCP_IOMFB_FORMAT_BGRA, true, &full));
        assert(memcmp(native.bytes, explicit_native.bytes, sizeof(native.bytes)) == 0);
        assert(u32at(native.bytes + 0xc4) == 0 && u32at(native.bytes + 0xc8) == 0);
        assert(u32at(native.bytes + 0xcc) == 2560 && u32at(native.bytes + 0xd0) == 1600);

        assert(dcp_iomfb_present_build_v13_5(&scaled, 0x12340000, 1920, 1080, 7680, DCP_IOMFB_FORMAT_BGRA, false, &fhd));
        assert(u32at(scaled.bytes + 0x64) == 0 && u32at(scaled.bytes + 0x68) == 0);
        assert(u32at(scaled.bytes + 0x6c) == 1920 && u32at(scaled.bytes + 0x70) == 1080);
        assert(u32at(scaled.bytes + 0xc4) == 0 && u32at(scaled.bytes + 0xc8) == 80);
        assert(u32at(scaled.bytes + 0xcc) == 2560 && u32at(scaled.bytes + 0xd0) == 1440);
        assert(u32at(scaled.bytes + 0x104) == 0x80000007u);
        assert(u32at(scaled.bytes + 0x108) == 0x80000007u);
        assert(u32at(scaled.bytes + 0x10c) == 0);
        assert(u32at(scaled.bytes + 0x468 + 0x15) == 7680);
        assert(u32at(scaled.bytes + 0x468 + 0x21) == 1920);
        assert(u32at(scaled.bytes + 0x468 + 0x25) == 1080);
        assert(u32at(scaled.bytes + 0x468 + 0x29) == 7680 * 1080);
        /* Everything outside the rectangle and background words matches the
         * native swap of the same source geometry. */
        assert(dcp_iomfb_present_build_v13_5(&native, 0x12340000, 1920, 1080, 7680, DCP_IOMFB_FORMAT_BGRA, false, NULL));
        assert(memcmp(native.bytes, scaled.bytes, 0xc4) == 0);
        assert(memcmp(native.bytes + 0x110, scaled.bytes + 0x110, sizeof(native.bytes) - 0x110) == 0);
        assert(u32at(native.bytes + 0x104) == 0x80000007u);

        assert(dcp_iomfb_present_build_v13_5(&scaled, 0x12340000, 1280, 720, 5120, DCP_IOMFB_FORMAT_RGBA, false, &centered));
        assert(u32at(scaled.bytes + 0xc4) == 640 && u32at(scaled.bytes + 0xc8) == 440);
        assert(u32at(scaled.bytes + 0xcc) == 1280 && u32at(scaled.bytes + 0xd0) == 720);
        assert(u32at(scaled.bytes + 0x104) == 0x80000007u);

        assert(!dcp_iomfb_present_build_v13_5(&scaled, 0x12340000, 1920, 1080, 7680, DCP_IOMFB_FORMAT_BGRA, false, &too_tall));
        assert(!dcp_iomfb_present_build_v13_5(&scaled, 0x12340000, 2560, 1600, 10240, DCP_IOMFB_FORMAT_BGRA, false, &too_wide));
        assert(!dcp_iomfb_present_build_v13_5(&scaled, 0x12340000, 1920, 1080, 7680, DCP_IOMFB_FORMAT_BGRA, false, &empty));
        assert(!dcp_iomfb_present_build_v13_5(&scaled, 0x12340000, 1920, 1080, 7680, DCP_IOMFB_FORMAT_BGRA, false, &wrapped));
        /* DCP's stride rule still applies to a small source. */
        assert(!dcp_iomfb_present_build_v13_5(&scaled, 0x12340000, 1366, 768, 5464, DCP_IOMFB_FORMAT_BGRA, false, &fhd));
    }
    /* R13: two-slot swap.  Offsets are the Asahi v13.x layout
     * (iomfb_template.h:20-130 with DCP_FW_VER 13.3 and the >= 13.2
     * branches; iomfb_plane.h:137-165), not this file's structs:
     * surf_ids 0x54, src_rect 0x64, surf_flags 0xa4, surf_unk 0xb4,
     * dst_rect 0xc4, swap_enabled 0x104, swap_completed 0x108,
     * surf[] 0x468 + i*0x22c, surf_iova 0xd18, surf_null 0x1877. */
    {
        struct dcp_iomfb_present_request one, two, off;
        struct dcp_iomfb_present_layer l[2];
        static const struct { size_t lo, hi; } slot1[] = {
            {0x58, 0x5c}, {0x74, 0x84}, {0xa8, 0xac}, {0xd4, 0xe4},
            {0x104, 0x10c}, {0x694, 0x8c0}, {0xd20, 0xd28}, {0x1878, 0x1879},
        };
        memset(l, 0, sizeof(l));
        l[0] = (struct dcp_iomfb_present_layer){
            .iova = 0x12340000, .width = 2560, .height = 1600, .stride = 10240,
            .dcp_format = DCP_IOMFB_FORMAT_BGRA, .source = {0, 0, 2560, 1600},
            .destination = {0, 0, 2560, 1600}, .surface_id = 3};
        l[1] = (struct dcp_iomfb_present_layer){
            .iova = 0x56780000, .width = 1280, .height = 720, .stride = 5120,
            .dcp_format = DCP_IOMFB_FORMAT_RGBA, .source = {0, 0, 1280, 720},
            .destination = {640, 440, 1280, 720}, .surface_id = 4,
            .premultiplied = true};
        assert(dcp_iomfb_present_build_v13_5(&one, 0x12340000, 2560, 1600, 10240, DCP_IOMFB_FORMAT_BGRA, false, NULL));
        assert(dcp_iomfb_present_build_layers_v13_5(&two, l, false));
        assert(u32at(two.bytes + 0x54) == 3 && u32at(two.bytes + 0x58) == 4);
        assert(u32at(two.bytes + 0x74) == 0 && u32at(two.bytes + 0x78) == 0);
        assert(u32at(two.bytes + 0x7c) == 1280 && u32at(two.bytes + 0x80) == 720);
        assert(u32at(two.bytes + 0xa4) == 1 && u32at(two.bytes + 0xa8) == 1);
        assert(u32at(two.bytes + 0xb8) == 0);
        assert(u32at(two.bytes + 0xd4) == 640 && u32at(two.bytes + 0xd8) == 440);
        assert(u32at(two.bytes + 0xdc) == 1280 && u32at(two.bytes + 0xe0) == 720);
        assert(u32at(two.bytes + 0x104) == 3 && u32at(two.bytes + 0x108) == 3);
        assert(two.bytes[0x694 + 0] == 0 && two.bytes[0x694 + 2] == 1);
        assert(u32at(two.bytes + 0x694 + 0x0b) == DCP_IOMFB_FORMAT_RGBA);
        assert(two.bytes[0x694 + 0x13] == 13 && two.bytes[0x694 + 0x14] == 1);
        assert(u32at(two.bytes + 0x694 + 0x15) == 5120);
        assert(two.bytes[0x694 + 0x19] == 4);
        assert(u32at(two.bytes + 0x694 + 0x21) == 1280);
        assert(u32at(two.bytes + 0x694 + 0x25) == 720);
        assert(u32at(two.bytes + 0x694 + 0x29) == 5120 * 720);
        assert(u32at(two.bytes + 0x694 + 0x35) == 4);
        assert(u64at(two.bytes + 0x694 + 0x51) == 1);
        assert(u64at(two.bytes + 0x694 + 0x149) == 1);
        assert(u64at(two.bytes + 0xd18) == 0x12340000);
        assert(u64at(two.bytes + 0xd20) == 0x56780000);
        assert(two.bytes[0x1877] == 0 && two.bytes[0x1878] == 0);
        assert(two.bytes[0x1879] == 1 && two.bytes[0x187a] == 1);
        /* Slot 0 and every swap-global word equal the proven one-slot swap. */
        for (size_t i = 0; i < sizeof(one.bytes); ++i) {
            bool in_slot1 = false;
            for (size_t r = 0; r < sizeof(slot1) / sizeof(slot1[0]); ++r)
                in_slot1 |= i >= slot1[r].lo && i < slot1[r].hi;
            if (!in_slot1)
                assert(one.bytes[i] == two.bytes[i]);
        }
        /* Disabling slot 1 = enable bit set, surface null, nothing else. */
        l[1].iova = 0;
        assert(dcp_iomfb_present_build_layers_v13_5(&off, l, false));
        assert(u32at(off.bytes + 0x104) == 3 && u32at(off.bytes + 0x108) == 3);
        assert(off.bytes[0x1878] == 1);
        assert(memcmp(one.bytes, off.bytes, 0x104) == 0);
        assert(memcmp(one.bytes + 0x10c, off.bytes + 0x10c, sizeof(one.bytes) - 0x10c) == 0);
        l[1].iova = 0x56780000;
        /* Boot-surface clear keeps the one-slot form of the enable word. */
        assert(dcp_iomfb_present_build_layers_v13_5(&two, l, true));
        assert(u32at(two.bytes + 0x104) == 0x80000007u && two.bytes[0x1878] == 0);
        /* Refusals: DCP limits from Asahi plane.c and the slot contract. */
        l[1].destination = (struct dcp_iomfb_present_rect){0, 0, 31, 720};
        l[1].source = (struct dcp_iomfb_present_rect){0, 0, 31, 720};
        assert(!dcp_iomfb_present_build_layers_v13_5(&two, l, false));
        l[1].source = (struct dcp_iomfb_present_rect){0, 0, 640, 360};
        l[1].destination = (struct dcp_iomfb_present_rect){0, 0, 1280, 720};
        assert(dcp_iomfb_present_build_layers_v13_5(&two, l, false));   /* 2x up */
        l[1].destination = (struct dcp_iomfb_present_rect){0, 0, 1281, 720};
        assert(!dcp_iomfb_present_build_layers_v13_5(&two, l, false));  /* > 2x */
        l[1].source = (struct dcp_iomfb_present_rect){0, 0, 1280, 720};
        l[1].destination = (struct dcp_iomfb_present_rect){0, 0, 640, 360};
        assert(dcp_iomfb_present_build_layers_v13_5(&two, l, false));   /* 2x down */
        l[1].destination = (struct dcp_iomfb_present_rect){0, 0, 639, 360};
        assert(!dcp_iomfb_present_build_layers_v13_5(&two, l, false));
        l[1].destination = (struct dcp_iomfb_present_rect){1281, 0, 1280, 720};
        assert(!dcp_iomfb_present_build_layers_v13_5(&two, l, false));  /* off panel */
        l[1].destination = (struct dcp_iomfb_present_rect){640, 440, 1280, 720};
        l[1].source = (struct dcp_iomfb_present_rect){1, 0, 1280, 720};
        assert(!dcp_iomfb_present_build_layers_v13_5(&two, l, false));  /* outside surface */
        l[1].source = (struct dcp_iomfb_present_rect){0, 0, 1280, 720};
        l[1].surface_id = 3;
        assert(!dcp_iomfb_present_build_layers_v13_5(&two, l, false));  /* duplicate id */
        l[1].surface_id = 4;
        l[1].stride = 5128;
        assert(!dcp_iomfb_present_build_layers_v13_5(&two, l, false));  /* stride & 63 */
        l[1].stride = 5120;
        l[0].iova = 0;
        assert(!dcp_iomfb_present_build_layers_v13_5(&two, l, false));  /* slot 0 required */
        l[0].iova = 0x12340000;
        assert(dcp_iomfb_present_build_layers_v13_5(&two, l, false));
    }
    test_one_slot_records_are_the_bench_records();
    puts("dcp_iomfb_present_test: ok");
    return 0;
}
