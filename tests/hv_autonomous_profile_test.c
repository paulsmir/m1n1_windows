#include <assert.h>
#include <stdio.h>

#include "../src/hv_autonomous_profile.h"

int main(void)
{
    struct hv_autonomous_profile profile;
    struct hv_autonomous_usb_plan usb;

    assert(hv_autonomous_flags_valid(HV_AUTONOMOUS_DISPLAY_PHYSICAL));
    assert(hv_autonomous_flags_valid(HV_AUTONOMOUS_DISPLAY_PHYSICAL |
                                     HV_AUTONOMOUS_DEBUG_MONITOR));
    assert(!hv_autonomous_flags_valid(HV_AUTONOMOUS_DEBUG_MASK));
    assert(!hv_autonomous_flags_valid(0x20));

    assert(hv_autonomous_profile_decode(HV_AUTONOMOUS_DISPLAY_PHYSICAL, &profile));
    assert(profile.physical_display);
    assert(!profile.virtual_display);
    assert(!profile.debug_host);
    assert(!profile.monitor);
    assert(!profile.telemetry);
    assert(!profile.proxy_takeover);
    assert(!hv_autonomous_profile_accept_proxy(&profile, true));
    assert(hv_autonomous_profile_usb_window_seconds(&profile) == 0);
    hv_autonomous_profile_usb_plan(&profile, &usb);
    assert(usb.power_platform);
    assert(!usb.start_debug_transport);

    assert(hv_autonomous_profile_decode(HV_AUTONOMOUS_DISPLAY_VIRTUAL |
                                            HV_AUTONOMOUS_DEBUG_UART,
                                        &profile));
    assert(!profile.physical_display);
    assert(profile.virtual_display);
    assert(profile.debug_host);
    assert(!profile.monitor);
    assert(!profile.telemetry);
    assert(profile.proxy_takeover);
    assert(hv_autonomous_profile_accept_proxy(&profile, true));
    assert(!hv_autonomous_profile_accept_proxy(&profile, false));
    assert(hv_autonomous_profile_usb_window_seconds(&profile) == 3);
    hv_autonomous_profile_usb_plan(&profile, &usb);
    assert(usb.power_platform);
    assert(usb.start_debug_transport);

    assert(hv_autonomous_profile_decode(HV_AUTONOMOUS_DISPLAY_MASK |
                                            HV_AUTONOMOUS_DEBUG_FULL,
                                        &profile));
    assert(profile.physical_display);
    assert(profile.virtual_display);
    assert(profile.debug_host);
    assert(!profile.monitor);
    assert(profile.telemetry);
    assert(profile.proxy_takeover);
    assert(hv_autonomous_profile_usb_window_seconds(&profile) == 3);
    hv_autonomous_profile_usb_plan(&profile, &usb);
    assert(usb.power_platform);
    assert(usb.start_debug_transport);

    assert(hv_autonomous_profile_decode(HV_AUTONOMOUS_DISPLAY_PHYSICAL |
                                            HV_AUTONOMOUS_DEBUG_MONITOR,
                                        &profile));
    assert(profile.physical_display);
    assert(!profile.virtual_display);
    assert(profile.debug_host);
    assert(profile.monitor);
    assert(!profile.telemetry);
    assert(!profile.proxy_takeover);
    assert(!hv_autonomous_profile_accept_proxy(&profile, true));
    assert(hv_autonomous_profile_usb_window_seconds(&profile) == 3);
    hv_autonomous_profile_usb_plan(&profile, &usb);
    assert(usb.power_platform);
    assert(usb.start_debug_transport);

    assert(!hv_autonomous_profile_decode(HV_AUTONOMOUS_DEBUG_MASK, &profile));
    assert(!hv_autonomous_profile_decode(0x14, &profile));
    assert(!hv_autonomous_profile_decode(0x18, &profile));
    assert(!hv_autonomous_profile_decode(0x1c, &profile));
    assert(!hv_autonomous_profile_decode(0, NULL));
    assert(!hv_autonomous_profile_accept_proxy(NULL, true));
    assert(hv_autonomous_profile_usb_window_seconds(NULL) == 0);

    puts("hv_autonomous_profile_test: ok");
    return 0;
}
