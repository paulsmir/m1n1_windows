/* SPDX-License-Identifier: MIT */

#ifndef HV_AUTONOMOUS_PROFILE_H
#define HV_AUTONOMOUS_PROFILE_H

#include "hv_autonomous_manifest.h"

struct hv_autonomous_profile {
    bool physical_display;
    bool virtual_display;
    bool debug_host;
    bool telemetry;
};

struct hv_autonomous_usb_plan {
    bool power_platform;
    bool start_debug_transport;
};

bool hv_autonomous_profile_decode(uint32_t flags, struct hv_autonomous_profile *out);
void hv_autonomous_profile_usb_plan(const struct hv_autonomous_profile *profile,
                                    struct hv_autonomous_usb_plan *out);

#endif
