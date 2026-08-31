/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../src/dcp_iomfb_present.h"
static uint32_t u32at(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t u64at(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }
int main(void)
{
    struct dcp_iomfb_present_request req;
    uint8_t start[0x18] = {0}, submit[0xc] = {0};
    uint32_t swap_id = 0;
    assert(!dcp_iomfb_present_build_v13_5(&req, 0x100000, 2560, 1600, 10176));
    assert(!dcp_iomfb_present_build_v13_5(&req, 0x100000, UINT32_MAX, 2, UINT32_MAX));
    assert(dcp_iomfb_present_build_v13_5(&req, 0x12340000, 2560, 1600, 10240));
    assert(u32at(req.bytes + 0x104) == 1 && u32at(req.bytes + 0x108) == 1);
    assert(u32at(req.bytes + 0x64 + 8) == 2560);
    assert(u32at(req.bytes + 0x64 + 12) == 1600);
    assert(u64at(req.bytes + 0xd18) == 0x12340000);
    assert(req.bytes[0x1876] == 0 && req.bytes[0x1877] == 0);
    assert(req.bytes[0x1878] == 1 && req.bytes[0x187b] == 1);
    assert(req.bytes[0x1880] == 0 && req.bytes[0x1881] == 1);
    assert(req.bytes[0x1882] == 1 && req.bytes[0x1883] == 0);
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
    puts("dcp_iomfb_present_test: ok");
    return 0;
}
