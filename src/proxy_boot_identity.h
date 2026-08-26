/* SPDX-License-Identifier: MIT */

#ifndef __PROXY_BOOT_IDENTITY_H__
#define __PROXY_BOOT_IDENTITY_H__

#ifdef PROXY_BOOT_IDENTITY_HOST_TEST
#include <stdint.h>
typedef uint64_t proxy_boot_u64;
#else
#include "types.h"
typedef u64 proxy_boot_u64;
#endif

void proxy_boot_identity_init(proxy_boot_u64 sample);
proxy_boot_u64 proxy_boot_identity_get(void);

#endif
