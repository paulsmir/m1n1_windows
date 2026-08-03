/* SPDX-License-Identifier: MIT */

#ifndef HV_AUTONOMOUS_H
#define HV_AUTONOMOUS_H

#include "hv_autonomous_manifest.h"

#include <stdbool.h>
#include <stdint.h>

enum hv_autonomous_stage {
    HV_AUTONOMOUS_STAGE_VALIDATE = 0,
    HV_AUTONOMOUS_STAGE_DECOMPRESS,
    HV_AUTONOMOUS_STAGE_BOOT_DATA,
    HV_AUTONOMOUS_STAGE_STAGE2,
    HV_AUTONOMOUS_STAGE_VGIC,
    HV_AUTONOMOUS_STAGE_PCI_NVME,
    HV_AUTONOMOUS_STAGE_XHCI,
    HV_AUTONOMOUS_STAGE_VUART,
    HV_AUTONOMOUS_STAGE_READY,
    HV_AUTONOMOUS_STAGE_ENTERED,
    HV_AUTONOMOUS_STAGE_COUNT,
};

enum hv_autonomous_result {
    HV_AUTONOMOUS_RESULT_OK = 0,
    HV_AUTONOMOUS_RESULT_INVALID_ARGUMENT,
    HV_AUTONOMOUS_RESULT_STAGE_FAILED,
};

struct hv_autonomous_status {
    enum hv_autonomous_stage stage;
    enum hv_autonomous_result error;
    bool guest_running;
    uint64_t firmware_entry;
    uint32_t layout_version;
};

typedef bool (*hv_autonomous_stage_fn)(enum hv_autonomous_stage stage,
                                       const struct hv_autonomous_payload *payload,
                                       struct hv_autonomous_status *status, void *opaque);

struct hv_autonomous_ops {
    hv_autonomous_stage_fn callbacks[HV_AUTONOMOUS_STAGE_COUNT];
};

enum hv_autonomous_result hv_autonomous_prepare_with_ops(
    const struct hv_autonomous_payload *payload, struct hv_autonomous_status *status,
    const struct hv_autonomous_ops *ops, void *opaque);

enum hv_autonomous_result hv_autonomous_prepare(const struct hv_autonomous_payload *payload,
                                                struct hv_autonomous_status *status);

#endif
