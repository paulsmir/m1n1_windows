/* SPDX-License-Identifier: MIT */

#include "hv_autonomous.h"

static void initialize_status(struct hv_autonomous_status *status,
                              const struct hv_autonomous_payload *payload)
{
    status->stage = HV_AUTONOMOUS_STAGE_VALIDATE;
    status->error = HV_AUTONOMOUS_RESULT_OK;
    status->guest_running = false;
    status->firmware_entry = 0;
    status->layout_version = payload ? payload->layout_version : 0;
}

enum hv_autonomous_result hv_autonomous_prepare_with_ops(
    const struct hv_autonomous_payload *payload, struct hv_autonomous_status *status,
    const struct hv_autonomous_ops *ops, void *opaque)
{
    if (status)
        initialize_status(status, payload);
    if (!payload || !status || !ops) {
        if (status)
            status->error = HV_AUTONOMOUS_RESULT_INVALID_ARGUMENT;
        return HV_AUTONOMOUS_RESULT_INVALID_ARGUMENT;
    }

    for (size_t index = 0; index < HV_AUTONOMOUS_STAGE_COUNT; index++) {
        enum hv_autonomous_stage stage = (enum hv_autonomous_stage)index;
        hv_autonomous_stage_fn callback = ops->callbacks[index];

        status->stage = stage;
        if (!callback || !callback(stage, payload, status, opaque)) {
            status->error = HV_AUTONOMOUS_RESULT_STAGE_FAILED;
            return status->error;
        }
    }

    status->guest_running = true;
    return HV_AUTONOMOUS_RESULT_OK;
}
