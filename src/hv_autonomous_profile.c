/* SPDX-License-Identifier: MIT */

#include "hv_autonomous_profile.h"

bool hv_autonomous_profile_decode(uint32_t flags, struct hv_autonomous_profile *out)
{
    uint32_t debug = flags & HV_AUTONOMOUS_DEBUG_MASK;

    if (!out || (flags & ~HV_AUTONOMOUS_KNOWN_FLAGS) ||
        (debug != 0 && debug != HV_AUTONOMOUS_DEBUG_UART &&
         debug != HV_AUTONOMOUS_DEBUG_FULL && debug != HV_AUTONOMOUS_DEBUG_MONITOR))
        return false;

    out->physical_display = flags & HV_AUTONOMOUS_DISPLAY_PHYSICAL;
    out->virtual_display = flags & HV_AUTONOMOUS_DISPLAY_VIRTUAL;
    out->debug_host = debug != 0;
    out->monitor = debug == HV_AUTONOMOUS_DEBUG_MONITOR;
    out->telemetry = debug == HV_AUTONOMOUS_DEBUG_FULL;
    out->proxy_takeover =
        debug == HV_AUTONOMOUS_DEBUG_UART || debug == HV_AUTONOMOUS_DEBUG_FULL;
    return true;
}

bool hv_autonomous_profile_accept_proxy(const struct hv_autonomous_profile *profile,
                                        bool host_connected)
{
    return profile && host_connected && profile->proxy_takeover;
}

uint32_t hv_autonomous_profile_usb_window_seconds(
    const struct hv_autonomous_profile *profile)
{
    /* USB CDC endpoints only become observable after the host has completed
     * enumeration.  Every profile that starts the debug transport must keep
     * servicing it for the same bounded window.  Whether an enumerated host
     * may take control is a separate policy enforced by accept_proxy(). */
    return profile && (profile->debug_host || profile->virtual_display) ? 3u : 0u;
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
