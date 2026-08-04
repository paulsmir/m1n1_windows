/* SPDX-License-Identifier: MIT */

#include "hv_autonomous_profile.h"

bool hv_autonomous_profile_decode(uint32_t flags, struct hv_autonomous_profile *out)
{
    if (!out || (flags & ~HV_AUTONOMOUS_KNOWN_FLAGS) ||
        (flags & HV_AUTONOMOUS_DEBUG_MASK) == HV_AUTONOMOUS_DEBUG_MASK)
        return false;

    out->physical_display = flags & HV_AUTONOMOUS_DISPLAY_PHYSICAL;
    out->virtual_display = flags & HV_AUTONOMOUS_DISPLAY_VIRTUAL;
    out->debug_host = flags & HV_AUTONOMOUS_DEBUG_MASK;
    out->telemetry = (flags & HV_AUTONOMOUS_DEBUG_MASK) == HV_AUTONOMOUS_DEBUG_FULL;
    return true;
}
