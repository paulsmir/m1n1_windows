/* SPDX-License-Identifier: MIT */

#include "hv_autonomous_boot.h"

enum hv_autonomous_boot_result hv_autonomous_boot_poll(
    uint64_t deadline_ticks, const struct hv_autonomous_boot_ops *ops,
    const struct hv_autonomous_payload *payload, struct hv_autonomous_status *status,
    void *opaque)
{
    if (!payload || !payload->compressed || !payload->compressed_size || !status || !ops ||
        !ops->now || !ops->command || !ops->service || !ops->launch || !ops->proxy)
        return HV_AUTONOMOUS_BOOT_INVALID;

    uint64_t start = ops->now(opaque);
    for (;;) {
        ops->service(opaque);
        switch (ops->command(opaque)) {
            case HV_AUTONOMOUS_COMMAND_PROXY:
                ops->proxy(opaque);
                return HV_AUTONOMOUS_BOOT_PROXY;
            case HV_AUTONOMOUS_COMMAND_HOLD:
                return HV_AUTONOMOUS_BOOT_HELD;
            case HV_AUTONOMOUS_COMMAND_RELEASE:
                return ops->launch(payload, status, opaque) ? HV_AUTONOMOUS_BOOT_LAUNCHED
                                                            : HV_AUTONOMOUS_BOOT_FAILED;
            case HV_AUTONOMOUS_COMMAND_NONE:
                break;
        }

        if (ops->now(opaque) - start >= deadline_ticks)
            return ops->launch(payload, status, opaque) ? HV_AUTONOMOUS_BOOT_LAUNCHED
                                                        : HV_AUTONOMOUS_BOOT_FAILED;
    }
}
