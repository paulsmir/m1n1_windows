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
    puts("dcp_iomfb_present_test: ok");
    return 0;
}
