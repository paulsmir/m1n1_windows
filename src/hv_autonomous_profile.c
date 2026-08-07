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

void hv_autonomous_profile_usb_plan(const struct hv_autonomous_profile *profile,
                                    struct hv_autonomous_usb_plan *out)
{
    /* The J313 guest xHCI shares the platform Type-C/DRD power sequence with
     * m1n1's optional debug transport.  A quiet production profile may skip
     * the gadget transport and host wait, but it must still power the PHY,
     * DART and DRD domains before Mu or Windows touches xHCI registers. */
    out->power_platform = true;
    out->start_debug_transport = profile->debug_host || profile->virtual_display;
}
