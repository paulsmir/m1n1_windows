/* SPDX-License-Identifier: MIT */

#include "boot_options.h"

#ifdef BOOT_OPTIONS_HOST_TEST
#include <stddef.h>
#endif

static bool is_separator(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0';
}

bool boot_option_skip_display(const char *cmdline)
{
    static const char option[] = "m1n1.nodisplay";
    const size_t option_len = sizeof(option) - 1;

    if (!cmdline)
        return false;

    while (*cmdline) {
        while (*cmdline && is_separator(*cmdline))
            cmdline++;

        const char *token = cmdline;
        size_t length = 0;
        while (*cmdline && !is_separator(*cmdline)) {
            cmdline++;
            length++;
        }

        if (length != option_len)
            continue;

        size_t i = 0;
        for (; i < option_len && token[i] == option[i]; i++)
            ;
        if (i == option_len)
            return true;
    }

    return false;
}
