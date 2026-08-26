/* SPDX-License-Identifier: MIT */

#ifndef BOOT_OPTIONS_H
#define BOOT_OPTIONS_H

#ifdef BOOT_OPTIONS_HOST_TEST
#include <stdbool.h>
#else
#include "types.h"
#endif

bool boot_option_skip_display(const char *cmdline);

#endif
