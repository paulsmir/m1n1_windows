#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/chainload_layout.h"

static void expect_failure(size_t image_and_vars, size_t sepfw_size,
                           size_t preoslog_size, size_t stub_size)
{
    struct chainload_layout layout = {0};

    assert(!chainload_layout_compute(image_and_vars, sepfw_size, preoslog_size,
                                     stub_size, &layout));
}

int main(void)
{
    struct chainload_layout layout = {0};

    assert(chainload_layout_compute(0x8000, 0x2000, 0, 0x180, &layout));
    assert(layout.sepfw_offset == 0x8000);
    assert(layout.preoslog_offset == 0xc000);
    assert(layout.bootargs_offset == 0xc000);
    assert(layout.stub_offset == 0x10000);
    assert(layout.copy_size == 0x10000);
    assert(layout.allocation_size == 0x10180);

    assert(chainload_layout_compute(0x4001, 1, 1, 0x100, &layout));
    assert(layout.sepfw_offset == 0x8000);
    assert(layout.preoslog_offset == 0xc000);
    assert(layout.bootargs_offset == 0x10000);
    assert(layout.stub_offset == 0x14000);
    assert(layout.copy_size == 0x14000);
    assert(layout.allocation_size == 0x14100);

    assert(chainload_layout_compute(0x4000, 0, 0, 1, &layout));
    assert(layout.sepfw_offset == 0x4000);
    assert(layout.preoslog_offset == 0x4000);
    assert(layout.bootargs_offset == 0x4000);
    assert(layout.stub_offset == 0x8000);
    assert(layout.copy_size == 0x8000);
    assert(layout.allocation_size == 0x8001);

    assert(!chainload_layout_compute(0x4000, 0, 0, 1, NULL));

    expect_failure(SIZE_MAX, 0, 0, 1);
    expect_failure(0x4000, SIZE_MAX, 0, 1);
    expect_failure(0x4000, SIZE_MAX - 0x4000, 0, 1);
    expect_failure(0x4000, 0, SIZE_MAX, 1);
    expect_failure(0x4000, 0, SIZE_MAX - 0x4000, 1);
    expect_failure(SIZE_MAX & ~(size_t)0x3fff, 0, 0, 1);
    expect_failure((SIZE_MAX & ~(size_t)0x3fff) - 0x4000, 0, 0, 0x4000);

    puts("chainload_layout_test: ok");
    return 0;
}
