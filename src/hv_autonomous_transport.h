/* SPDX-License-Identifier: MIT */

#ifndef HV_AUTONOMOUS_TRANSPORT_H
#define HV_AUTONOMOUS_TRANSPORT_H

static inline int hv_autonomous_debug_transport(int candidate, int first_usb, int usb_count)
{
    return candidate >= first_usb && candidate < first_usb + usb_count ? candidate : -1;
}

#endif
