/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/dcp_iomfb_bootstrap.h"

struct fixture {
    char tags[32][5];
    uint32_t input_sizes[32];
    uint32_t output_sizes[32];
    unsigned int calls;
    unsigned int platform_calls;
    unsigned int last_platform_id;
};

static bool record_call(void *opaque, const char tag[4], const void *input,
                        uint32_t input_size, void *output,
                        uint32_t output_size)
{
    struct fixture *fixture = opaque;

    assert(fixture->calls < 32);
    memcpy(fixture->tags[fixture->calls], tag, 4);
    fixture->tags[fixture->calls][4] = 0;
    fixture->input_sizes[fixture->calls] = input_size;
    fixture->output_sizes[fixture->calls] = output_size;
    fixture->calls++;
    if (input_size)
        assert(input != NULL);
    if (output_size) {
        assert(output != NULL);
        memset(output, 0, output_size);
    }
    if (memcmp(tag, "A411", 4) == 0) {
        uint32_t main_display = 1;
        memcpy(output, &main_display, sizeof(main_display));
    }
    return true;
}

static int platform_callback(void *opaque, unsigned int callback_id,
                             const void *input, uint32_t input_size,
                             void *output, uint32_t output_size)
{
    struct fixture *fixture = opaque;

    fixture->platform_calls++;
    fixture->last_platform_id = callback_id;
    if (callback_id == 589) {
        assert(input_size == 0x6f0 && output_size == 0);
        assert(input != NULL && output == NULL);
        return 0;
    }
    assert(callback_id == 3);
    assert(input_size == 4 && output_size == 0x3c);
    (void)input;
    memset(output, 0xa5, output_size);
    return 0;
}

static void expect_call(const struct fixture *fixture, unsigned int index,
                        const char tag[4], uint32_t input_size,
                        uint32_t output_size)
{
    assert(index < fixture->calls);
    assert(memcmp(fixture->tags[index], tag, 4) == 0);
    assert(fixture->input_sizes[index] == input_size);
    assert(fixture->output_sizes[index] == output_size);
}

int main(void)
{
    struct fixture fixture;
    struct dcp_iomfb_bootstrap bootstrap;
    uint8_t output[0x3c];
    uint8_t d589[0x6f0];
    uint8_t d124_input[0x64];
    uint8_t d124_output[0x24];

    memset(&fixture, 0, sizeof(fixture));
    dcp_iomfb_bootstrap_init(&bootstrap, record_call, platform_callback,
                             &fixture);

    assert(dcp_iomfb_bootstrap_callback(&bootstrap, "D120", NULL, 0,
                                         output, 4) == 0);
    assert(output[0] == 1);
    assert(fixture.calls == 6);
    expect_call(&fixture, 0, "A373", 0, 0);
    expect_call(&fixture, 1, "A445", 0, 4);
    expect_call(&fixture, 2, "A029", 0, 0);
    expect_call(&fixture, 3, "A466", 4, 0);
    expect_call(&fixture, 4, "A000", 4, 4);
    expect_call(&fixture, 5, "A463", 0, 4);

    fixture.calls = 0;
    assert(dcp_iomfb_bootstrap_start(&bootstrap));
    assert(dcp_iomfb_bootstrap_state(&bootstrap) == DCP_IOMFB_BOOT_ACTIVE);
    assert(dcp_iomfb_bootstrap_main_display(&bootstrap));
    assert(fixture.calls == 5);
    expect_call(&fixture, 0, "A401", 0, 4);
    expect_call(&fixture, 1, "A426", 8, 8);
    expect_call(&fixture, 2, "A449", 4, 4);
    expect_call(&fixture, 3, "A456", 0, 0);
    expect_call(&fixture, 4, "A411", 0, 4);

    memset(output, 0, sizeof(output));
    assert(dcp_iomfb_bootstrap_callback(&bootstrap, "D003", output, 4,
                                         output, sizeof(output)) == 0);
    assert(output[0] == 0xa5);
    assert(fixture.platform_calls == 1);

    assert(dcp_iomfb_bootstrap_callback(&bootstrap, "D100", NULL, 0,
                                         NULL, 0) == 0);
    expect_call(&fixture, fixture.calls - 1, "A374", 0, 4);

    memset(output, 0, sizeof(output));
    assert(dcp_iomfb_bootstrap_callback(&bootstrap, "D206", NULL, 0,
                                         output, 4) == 0);
    assert(output[0] == 1);
    expect_call(&fixture, fixture.calls - 1, "A131", 0, 4);

    memset(output, 0, sizeof(output));
    assert(dcp_iomfb_bootstrap_callback(&bootstrap, "D207", NULL, 0,
                                         output, 4) == 0);
    assert(output[0] == 1);
    expect_call(&fixture, fixture.calls - 1, "A132", 0, 4);

    memset(output, 0, sizeof(output));
    assert(dcp_iomfb_bootstrap_callback(&bootstrap, "D101", NULL, 0,
                                         output, 4) == 0);
    assert(output[0] == 0);

    memset(d589, 0, sizeof(d589));
    assert(dcp_iomfb_bootstrap_callback(&bootstrap, "D589", d589, 0x6f0,
                                         NULL, 0) == 0);
    assert(fixture.last_platform_id == 589);
    assert(fixture.platform_calls == 2);

    memset(d124_input, 0, sizeof(d124_input));
    memset(d124_output, 0, sizeof(d124_output));
    for (unsigned int i = 0; i < 8; i++) {
        uint32_t word = 0x12400000u + i;
        memcpy(d124_input + 0x44 + i * sizeof(word), &word, sizeof(word));
    }
    assert(dcp_iomfb_bootstrap_callback(&bootstrap, "D124", d124_input,
                                         sizeof(d124_input), d124_output,
                                         sizeof(d124_output)) == 0);
    assert(memcmp(d124_output, d124_input + 0x44, 8 * sizeof(uint32_t)) == 0);
    assert(d124_output[0x20] == 0);

    assert(dcp_iomfb_bootstrap_callback(&bootstrap, "D454", output, 4,
                                         output, 4) < 0);
    assert(dcp_iomfb_bootstrap_state(&bootstrap) == DCP_IOMFB_BOOT_FAILED);

    puts("dcp_iomfb_bootstrap_test: ok");
    return 0;
}
