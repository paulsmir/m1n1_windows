/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/dcp_iomfb_v13_5_abi.h"

static void expect_callback(unsigned int id, uint32_t input, uint32_t output)
{
    struct dcp_iomfb_abi_size size;

    assert(dcp_iomfb_v13_5_callback_size(id, &size));
    assert(size.input == input);
    assert(size.output == output);
}

static void expect_method(unsigned int id, uint32_t input, uint32_t output)
{
    struct dcp_iomfb_abi_size size;

    assert(dcp_iomfb_v13_5_method_size(id, &size));
    assert(size.input == input);
    assert(size.output == output);
}

int main(void)
{
    struct dcp_iomfb_abi_size size;

    expect_method(401, 0x0, 0x4);
    expect_method(373, 0x0, 0x0);
    expect_method(426, 0x8, 0x8);
    expect_method(410, 0x4, 0x4);
    expect_method(412, 0x8, 0x4);
    expect_method(472, 0xc, 0x8);
    expect_callback(3, 0x4, 0x3c);
    expect_callback(120, 0x0, 0x4);
    expect_callback(411, 0x10, 0x1c);
    expect_callback(454, 0x8, 0x4);
    expect_callback(456, 0x4, 0x4);
    expect_callback(576, 0x54, 0x4c);
    expect_callback(589, 0x6f0, 0x0);
    assert(!dcp_iomfb_v13_5_callback_size(119, &size));
    assert(!dcp_iomfb_v13_5_callback_size(999, &size));
    assert(!dcp_iomfb_v13_5_method_size(999, &size));

    puts("dcp_iomfb_v13_5_abi_test: ok");
    return 0;
}
