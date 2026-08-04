#include <assert.h>
#include <stdio.h>

#include "../src/hv_autonomous_profile.h"

int main(void)
{
    struct hv_autonomous_profile profile;

    assert(hv_autonomous_profile_decode(HV_AUTONOMOUS_DISPLAY_PHYSICAL, &profile));
    assert(profile.physical_display);
    assert(!profile.virtual_display);
    assert(!profile.debug_host);
    assert(!profile.telemetry);

    assert(hv_autonomous_profile_decode(HV_AUTONOMOUS_DISPLAY_VIRTUAL |
                                            HV_AUTONOMOUS_DEBUG_UART,
                                        &profile));
    assert(!profile.physical_display);
    assert(profile.virtual_display);
    assert(profile.debug_host);
    assert(!profile.telemetry);

    assert(hv_autonomous_profile_decode(HV_AUTONOMOUS_DISPLAY_MASK |
                                            HV_AUTONOMOUS_DEBUG_FULL,
                                        &profile));
    assert(profile.physical_display);
    assert(profile.virtual_display);
    assert(profile.debug_host);
    assert(profile.telemetry);

    assert(!hv_autonomous_profile_decode(HV_AUTONOMOUS_DEBUG_MASK, &profile));
    assert(!hv_autonomous_profile_decode(0x10, &profile));
    assert(!hv_autonomous_profile_decode(0, NULL));

    puts("hv_autonomous_profile_test: ok");
    return 0;
}
