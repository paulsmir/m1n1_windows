/* SPDX-License-Identifier: MIT */

#include "proxy_boot_identity.h"

static proxy_boot_u64 boot_cookie;

void proxy_boot_identity_init(proxy_boot_u64 sample)
{
    if (!boot_cookie)
        boot_cookie = sample ? sample : 1;
}

proxy_boot_u64 proxy_boot_identity_get(void)
{
    return boot_cookie;
}
