/* SPDX-License-Identifier: MIT */

#ifndef HV_AUTONOMOUS_PROFILE_H
#define HV_AUTONOMOUS_PROFILE_H

#include "hv_autonomous_manifest.h"

struct hv_autonomous_profile {
    bool physical_display;
    bool virtual_display;
    bool debug_host;
    bool monitor;
    bool telemetry;
    bool proxy_takeover;
};

struct hv_autonomous_usb_plan {
    bool power_platform;
    bool start_debug_transport;
};

bool hv_autonomous_profile_decode(uint32_t flags, struct hv_autonomous_profile *out);
bool hv_autonomous_profile_accept_proxy(const struct hv_autonomous_profile *profile,
                                        bool host_connected);
void hv_autonomous_profile_usb_plan(const struct hv_autonomous_profile *profile,
                                    struct hv_autonomous_usb_plan *out);

#endif
