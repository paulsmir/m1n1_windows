/* SPDX-License-Identifier: MIT */

#ifndef HV_AUTONOMOUS_BOOT_RUNTIME_H
#define HV_AUTONOMOUS_BOOT_RUNTIME_H

#include <stdbool.h>

enum hv_autonomous_boot_attempt {
    HV_AUTONOMOUS_BOOT_ABSENT = 0,
    HV_AUTONOMOUS_BOOT_HANDLED,
    HV_AUTONOMOUS_BOOT_ATTEMPT_FAILED,
};

enum hv_autonomous_boot_attempt hv_autonomous_boot_if_present(bool *usb_up);

#endif
