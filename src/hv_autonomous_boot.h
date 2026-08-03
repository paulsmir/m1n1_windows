/* SPDX-License-Identifier: MIT */

#ifndef HV_AUTONOMOUS_BOOT_H
#define HV_AUTONOMOUS_BOOT_H

#include "hv_autonomous.h"

enum hv_autonomous_command {
    HV_AUTONOMOUS_COMMAND_NONE = 0,
    HV_AUTONOMOUS_COMMAND_HOLD,
    HV_AUTONOMOUS_COMMAND_RELEASE,
    HV_AUTONOMOUS_COMMAND_PROXY,
};

enum hv_autonomous_boot_result {
    HV_AUTONOMOUS_BOOT_LAUNCHED = 0,
    HV_AUTONOMOUS_BOOT_PROXY,
    HV_AUTONOMOUS_BOOT_HELD,
    HV_AUTONOMOUS_BOOT_INVALID,
    HV_AUTONOMOUS_BOOT_FAILED,
};

struct hv_autonomous_boot_ops {
    uint64_t (*now)(void *opaque);
    enum hv_autonomous_command (*command)(void *opaque);
    void (*service)(void *opaque);
    bool (*launch)(const struct hv_autonomous_payload *payload,
                   struct hv_autonomous_status *status, void *opaque);
    void (*proxy)(void *opaque);
};

enum hv_autonomous_boot_result hv_autonomous_boot_poll(
    uint64_t deadline_ticks, const struct hv_autonomous_boot_ops *ops,
    const struct hv_autonomous_payload *payload, struct hv_autonomous_status *status,
    void *opaque);

#endif
