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

bool hv_autonomous_profile_decode(uint32_t flags, struct hv_autonomous_profile *out);

#endif
