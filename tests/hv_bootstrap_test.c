#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/hv_bootstrap.h"

struct fake_runtime {
    char calls[8];
    size_t call_count;
    uint8_t destination[32];
    bool allocate_ok;
    bool decompress_ok;
    bool consume_all;
    bool produce_all;
    uint32_t crc32;
    int chainload_result;
};

static void record(struct fake_runtime *runtime, char call)
{
    runtime->calls[runtime->call_count++] = call;
}

static void *fake_allocate(size_t size, size_t alignment, void *opaque)
{
    struct fake_runtime *runtime = opaque;
    record(runtime, 'A');
    assert(size == sizeof(runtime->destination));
    assert(alignment == 0x4000);
    return runtime->allocate_ok ? runtime->destination : NULL;
}

static bool fake_decompress(const void *source, uint32_t *source_size, void *destination,
                            uint32_t *destination_size, void *opaque)
{
    struct fake_runtime *runtime = opaque;
    record(runtime, 'D');
    assert(source != NULL);
    assert(destination == runtime->destination);
    if (runtime->consume_all)
        *source_size = 8;
    else
        *source_size = 7;
    if (runtime->produce_all)
        *destination_size = sizeof(runtime->destination);
    else
        *destination_size = sizeof(runtime->destination) - 1;
    return runtime->decompress_ok;
}

static uint32_t fake_crc32(const void *data, size_t size, void *opaque)
{
    struct fake_runtime *runtime = opaque;
    record(runtime, 'C');
    assert(data == runtime->destination);
    assert(size == sizeof(runtime->destination));
    return runtime->crc32;
}

static int fake_chainload(void *image, size_t size, void *opaque)
{
    struct fake_runtime *runtime = opaque;
    record(runtime, 'L');
    assert(image == runtime->destination);
    assert(size == sizeof(runtime->destination));
    return runtime->chainload_result;
}

static const struct hv_bootstrap_ops fake_ops = {
    .allocate = fake_allocate,
    .decompress = fake_decompress,
    .crc32 = fake_crc32,
    .chainload = fake_chainload,
};

static struct fake_runtime valid_runtime(void)
{
    return (struct fake_runtime){
        .allocate_ok = true,
        .decompress_ok = true,
        .consume_all = true,
        .produce_all = true,
        .crc32 = 0x12345678,
        .chainload_result = 0,
    };
}

static struct hv_bootstrap_payload valid_payload(void)
{
    static const uint8_t compressed[8] = {0};
    return (struct hv_bootstrap_payload){
        .compressed = compressed,
        .compressed_size = sizeof(compressed),
        .uncompressed_size = 32,
        .crc32 = 0x12345678,
        .flags = HV_AUTONOMOUS_DISPLAY_PHYSICAL | HV_AUTONOMOUS_DEBUG_MONITOR,
    };
}

static void expect_result(struct fake_runtime *runtime, enum hv_bootstrap_result expected,
                          const char *calls)
{
    struct hv_bootstrap_payload payload = valid_payload();

    assert(hv_bootstrap_prepare_with_ops(&payload, &fake_ops, runtime) == expected);
    assert(runtime->call_count == strlen(calls));
    assert(memcmp(runtime->calls, calls, runtime->call_count) == 0);
}

int main(void)
{
    struct fake_runtime runtime = valid_runtime();
    expect_result(&runtime, HV_BOOTSTRAP_RESULT_OK, "ADCL");

    runtime = valid_runtime();
    runtime.allocate_ok = false;
    expect_result(&runtime, HV_BOOTSTRAP_RESULT_ALLOCATE_FAILED, "A");

    runtime = valid_runtime();
    runtime.decompress_ok = false;
    expect_result(&runtime, HV_BOOTSTRAP_RESULT_DECOMPRESS_FAILED, "AD");

    runtime = valid_runtime();
    runtime.consume_all = false;
    expect_result(&runtime, HV_BOOTSTRAP_RESULT_SOURCE_SIZE, "AD");

    runtime = valid_runtime();
    runtime.produce_all = false;
    expect_result(&runtime, HV_BOOTSTRAP_RESULT_DESTINATION_SIZE, "AD");

    runtime = valid_runtime();
    runtime.crc32 ^= 1;
    expect_result(&runtime, HV_BOOTSTRAP_RESULT_CRC_MISMATCH, "ADC");

    runtime = valid_runtime();
    runtime.chainload_result = -1;
    expect_result(&runtime, HV_BOOTSTRAP_RESULT_CHAINLOAD_FAILED, "ADCL");

    struct hv_bootstrap_payload payload = valid_payload();
    assert(hv_bootstrap_prepare_with_ops(NULL, &fake_ops, &runtime) ==
           HV_BOOTSTRAP_RESULT_INVALID);
    assert(hv_bootstrap_prepare_with_ops(&payload, NULL, &runtime) ==
           HV_BOOTSTRAP_RESULT_INVALID);

    assert(hv_bootstrap_attempt_from_manifest_error(HV_BOOTSTRAP_ERROR_MAGIC) ==
           HV_BOOTSTRAP_ABSENT);
    assert(hv_bootstrap_attempt_from_manifest_error(HV_BOOTSTRAP_ERROR_VERSION) ==
           HV_BOOTSTRAP_ATTEMPT_FAILED);

    puts("hv_bootstrap_test: ok");
    return 0;
}
