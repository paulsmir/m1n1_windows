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
    uint8_t inputs[32][0x40];
    unsigned int calls;
    unsigned int platform_calls;
    unsigned int last_platform_id;
    bool force_bad_display_result;
    bool force_bad_parameter_result;
    bool force_bad_modeset_result;
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
    if (input_size) {
        assert(input_size <= sizeof(fixture->inputs[fixture->calls]));
        memcpy(fixture->inputs[fixture->calls], input, input_size);
    }
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
    } else if (memcmp(tag, "A472", 4) == 0) {
        uint32_t success = 0;
        memcpy((uint8_t *)output + sizeof(uint32_t), &success,
               sizeof(success));
    } else if (memcmp(tag, "A410", 4) == 0) {
        uint32_t result = fixture->force_bad_display_result ? 0 : 2;
        memcpy(output, &result, sizeof(result));
    } else if (memcmp(tag, "A441", 4) == 0) {
        uint32_t result = fixture->force_bad_parameter_result ? 1 : 0;
        memcpy(output, &result, sizeof(result));
    } else if (memcmp(tag, "A412", 4) == 0) {
        uint32_t result = fixture->force_bad_modeset_result ? 0 : 2;
        memcpy(output, &result, sizeof(result));
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
    if (callback_id == 563) {
        uint32_t success = 1;
        assert(input_size == 0x4c && output_size == 4);
        assert(input != NULL && output != NULL);
        memcpy(output, &success, sizeof(success));
        return 0;
    }
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
    uint8_t d576_input[0x54];
    uint8_t d576_output[0x4c];
    uint8_t d563_input[0x4c];

    memset(&fixture, 0, sizeof(fixture));
    dcp_iomfb_bootstrap_init(&bootstrap, record_call, platform_callback,
                             &fixture);

    memset(output, 0xff, 4);
    assert(dcp_iomfb_bootstrap_callback(&bootstrap, "D112", NULL, 0,
                                         output, 4) == 0);
    assert(output[0] == 0 && output[1] == 0 && output[2] == 0 &&
           output[3] == 0);

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

    memset(&fixture, 0, sizeof(fixture));
    dcp_iomfb_bootstrap_init(&bootstrap, record_call, platform_callback,
                             &fixture);
    assert(dcp_iomfb_bootstrap_start_through_color_remap(&bootstrap));
    assert(dcp_iomfb_bootstrap_state(&bootstrap) ==
           DCP_IOMFB_BOOT_POST_INIT);
    assert(fixture.calls == 2);
    expect_call(&fixture, 0, "A401", 0, 4);
    expect_call(&fixture, 1, "A426", 8, 8);

    memset(&fixture, 0, sizeof(fixture));
    dcp_iomfb_bootstrap_init(&bootstrap, record_call, platform_callback,
                             &fixture);
    assert(dcp_iomfb_bootstrap_start_through_video_power_savings(&bootstrap));
    assert(dcp_iomfb_bootstrap_state(&bootstrap) ==
           DCP_IOMFB_BOOT_POST_INIT);
    assert(fixture.calls == 3);
    expect_call(&fixture, 0, "A401", 0, 4);
    expect_call(&fixture, 1, "A426", 8, 8);
    expect_call(&fixture, 2, "A449", 4, 4);

    memset(&fixture, 0, sizeof(fixture));
    dcp_iomfb_bootstrap_init(&bootstrap, record_call, platform_callback,
                             &fixture);
    assert(dcp_iomfb_bootstrap_start_through_first_client_open(&bootstrap));
    assert(dcp_iomfb_bootstrap_state(&bootstrap) ==
           DCP_IOMFB_BOOT_POST_INIT);
    assert(fixture.calls == 4);
    expect_call(&fixture, 0, "A401", 0, 4);
    expect_call(&fixture, 1, "A426", 8, 8);
    expect_call(&fixture, 2, "A449", 4, 4);
    expect_call(&fixture, 3, "A456", 0, 0);

    memset(&fixture, 0, sizeof(fixture));
    dcp_iomfb_bootstrap_init(&bootstrap, record_call, platform_callback,
                             &fixture);
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

    assert(dcp_iomfb_bootstrap_power_on(&bootstrap));
    assert(dcp_iomfb_bootstrap_state(&bootstrap) == DCP_IOMFB_BOOT_POWERED);
    assert(fixture.calls == 7);
    expect_call(&fixture, 5, "A472", 12, 8);
    expect_call(&fixture, 6, "A410", 4, 4);
    assert(fixture.inputs[5][0] == 1);
    assert(fixture.inputs[6][0] == 0);
    for (unsigned int i = 1; i < 12; i++)
        assert(fixture.inputs[5][i] == 0);

    assert(dcp_iomfb_bootstrap_prepare_modeset(&bootstrap));
    assert(fixture.calls == 8);
    expect_call(&fixture, 7, "A441", 0x28, 4);
    assert(fixture.inputs[7][0] == 14);
    assert(fixture.inputs[7][0x24] == 1);
    assert(dcp_iomfb_bootstrap_modeset(&bootstrap, 7, 11));
    assert(dcp_iomfb_bootstrap_state(&bootstrap) == DCP_IOMFB_BOOT_MODESET);
    assert(fixture.calls == 9);
    expect_call(&fixture, 8, "A412", 8, 4);
    assert(fixture.inputs[8][0] == 7);
    assert(fixture.inputs[8][4] == 11);

    memset(&fixture, 0, sizeof(fixture));
    fixture.force_bad_modeset_result = true;
    dcp_iomfb_bootstrap_init(&bootstrap, record_call, platform_callback,
                             &fixture);
    assert(dcp_iomfb_bootstrap_start(&bootstrap));
    assert(dcp_iomfb_bootstrap_power_on(&bootstrap));
    assert(dcp_iomfb_bootstrap_prepare_modeset(&bootstrap));
    assert(!dcp_iomfb_bootstrap_modeset(&bootstrap, 7, 11));
    assert(dcp_iomfb_bootstrap_state(&bootstrap) == DCP_IOMFB_BOOT_FAILED);

    memset(&fixture, 0, sizeof(fixture));
    fixture.force_bad_parameter_result = true;
    dcp_iomfb_bootstrap_init(&bootstrap, record_call, platform_callback,
                             &fixture);
    assert(dcp_iomfb_bootstrap_start(&bootstrap));
    assert(dcp_iomfb_bootstrap_power_on(&bootstrap));
    assert(!dcp_iomfb_bootstrap_prepare_modeset(&bootstrap));
    assert(dcp_iomfb_bootstrap_state(&bootstrap) == DCP_IOMFB_BOOT_FAILED);

    memset(&fixture, 0, sizeof(fixture));
    dcp_iomfb_bootstrap_init(&bootstrap, record_call, platform_callback,
                             &fixture);
    assert(dcp_iomfb_bootstrap_start(&bootstrap));
    assert(dcp_iomfb_bootstrap_power_on_firmware(&bootstrap));
    assert(dcp_iomfb_bootstrap_state(&bootstrap) ==
           DCP_IOMFB_BOOT_POWER_ON);
    assert(fixture.calls == 6);
    expect_call(&fixture, 5, "A472", 12, 8);
    assert(dcp_iomfb_bootstrap_select_display(&bootstrap));
    assert(dcp_iomfb_bootstrap_state(&bootstrap) == DCP_IOMFB_BOOT_POWERED);
    assert(fixture.calls == 7);
    expect_call(&fixture, 6, "A410", 4, 4);

    memset(&fixture, 0, sizeof(fixture));
    fixture.force_bad_display_result = true;
    dcp_iomfb_bootstrap_init(&bootstrap, record_call, platform_callback,
                             &fixture);
    assert(dcp_iomfb_bootstrap_start(&bootstrap));
    assert(dcp_iomfb_bootstrap_power_on_firmware(&bootstrap));
    assert(!dcp_iomfb_bootstrap_select_display(&bootstrap));
    assert(dcp_iomfb_bootstrap_state(&bootstrap) == DCP_IOMFB_BOOT_FAILED);

    memset(output, 0, sizeof(output));
    assert(dcp_iomfb_bootstrap_callback(&bootstrap, "D003", output, 4,
                                         output, sizeof(output)) == 0);
    assert(output[0] == 0xa5);
    assert(fixture.platform_calls == 1);

    memset(d563_input, 0, sizeof(d563_input));
    memcpy(d563_input, "DPTimingModeId", 14);
    memset(output, 0, 4);
    assert(dcp_iomfb_bootstrap_callback(&bootstrap, "D563", d563_input,
                                         sizeof(d563_input), output, 4) == 0);
    assert(output[0] == 1);
    assert(fixture.platform_calls == 2 && fixture.last_platform_id == 563);

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
    assert(fixture.platform_calls == 3);

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

    memset(d576_input, 0, sizeof(d576_input));
    memset(d576_output, 0, sizeof(d576_output));
    for (unsigned int i = 0; i < sizeof(d576_output); i++)
        d576_input[4 + i] = (uint8_t)(0x40u + i);
    /* Offset 0x50 is the false/nullability flag for the in/out pointer. */
    d576_input[0x50] = 0;
    assert(dcp_iomfb_bootstrap_callback(&bootstrap, "D576", d576_input,
                                         sizeof(d576_input), d576_output,
                                         sizeof(d576_output)) == 0);
    assert(memcmp(d576_output, d576_input + 4, sizeof(d576_output)) == 0);

    d576_input[0x50] = 1;
    memset(d576_output, 0xff, sizeof(d576_output));
    assert(dcp_iomfb_bootstrap_callback(&bootstrap, "D576", d576_input,
                                         sizeof(d576_input), d576_output,
                                         sizeof(d576_output)) == 0);
    for (unsigned int i = 0; i < sizeof(d576_output); i++)
        assert(d576_output[i] == 0);

    assert(dcp_iomfb_bootstrap_callback(&bootstrap, "D454", output, 4,
                                         output, 4) < 0);
    assert(dcp_iomfb_bootstrap_state(&bootstrap) == DCP_IOMFB_BOOT_FAILED);

    puts("dcp_iomfb_bootstrap_test: ok");
    return 0;
}
