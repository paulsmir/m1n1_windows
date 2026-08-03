#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/hv_autonomous.h"

struct recorder {
    enum hv_autonomous_stage seen[HV_AUTONOMOUS_STAGE_COUNT];
    size_t count;
    enum hv_autonomous_stage fail_at;
};

static bool record_stage(enum hv_autonomous_stage stage,
                         const struct hv_autonomous_payload *payload,
                         struct hv_autonomous_status *status, void *opaque)
{
    struct recorder *recorder = opaque;

    assert(payload != NULL);
    assert(status->stage == stage);
    assert(!status->guest_running);
    assert(recorder->count < HV_AUTONOMOUS_STAGE_COUNT);
    recorder->seen[recorder->count++] = stage;
    if (stage == HV_AUTONOMOUS_STAGE_BOOT_DATA)
        status->firmware_entry = 0x8510b4000;
    return stage != recorder->fail_at;
}

static struct hv_autonomous_ops recording_ops(void)
{
    struct hv_autonomous_ops ops = {0};

    for (size_t i = 0; i < HV_AUTONOMOUS_STAGE_COUNT; i++)
        ops.callbacks[i] = record_stage;
    return ops;
}

int main(void)
{
    const struct hv_autonomous_payload payload = {
        .compressed = "xz",
        .compressed_size = 2,
        .uncompressed_size = 0x1e00000,
        .layout_version = 1,
    };
    const struct hv_autonomous_ops ops = recording_ops();
    struct hv_autonomous_status status;
    struct recorder recorder = {.fail_at = HV_AUTONOMOUS_STAGE_COUNT};

    assert(hv_autonomous_prepare_with_ops(&payload, &status, &ops, &recorder) ==
           HV_AUTONOMOUS_RESULT_OK);
    assert(recorder.count == HV_AUTONOMOUS_STAGE_COUNT);
    for (size_t i = 0; i < recorder.count; i++)
        assert(recorder.seen[i] == (enum hv_autonomous_stage)i);
    assert(status.stage == HV_AUTONOMOUS_STAGE_ENTERED);
    assert(status.error == HV_AUTONOMOUS_RESULT_OK);
    assert(status.guest_running);
    assert(status.firmware_entry == 0x8510b4000);
    assert(status.layout_version == 1);

    for (size_t failed = 0; failed < HV_AUTONOMOUS_STAGE_COUNT; failed++) {
        memset(&status, 0xa5, sizeof(status));
        memset(&recorder, 0, sizeof(recorder));
        recorder.fail_at = (enum hv_autonomous_stage)failed;

        assert(hv_autonomous_prepare_with_ops(&payload, &status, &ops, &recorder) ==
               HV_AUTONOMOUS_RESULT_STAGE_FAILED);
        assert(recorder.count == failed + 1);
        assert(status.stage == (enum hv_autonomous_stage)failed);
        assert(status.error == HV_AUTONOMOUS_RESULT_STAGE_FAILED);
        assert(!status.guest_running);
    }

    assert(hv_autonomous_prepare_with_ops(NULL, &status, &ops, &recorder) ==
           HV_AUTONOMOUS_RESULT_INVALID_ARGUMENT);
    assert(hv_autonomous_prepare_with_ops(&payload, NULL, &ops, &recorder) ==
           HV_AUTONOMOUS_RESULT_INVALID_ARGUMENT);
    assert(hv_autonomous_prepare_with_ops(&payload, &status, NULL, &recorder) ==
           HV_AUTONOMOUS_RESULT_INVALID_ARGUMENT);

    puts("hv_autonomous_stage_test: ok");
    return 0;
}
