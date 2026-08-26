/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "../src/boot_options.h"

int main(void)
{
    assert(!boot_option_skip_display(NULL));
    assert(!boot_option_skip_display(""));
    assert(!boot_option_skip_display("-v"));
    assert(boot_option_skip_display("m1n1.nodisplay"));
    assert(boot_option_skip_display("-v m1n1.nodisplay debug"));
    assert(boot_option_skip_display("\tm1n1.nodisplay\n"));
    assert(!boot_option_skip_display("m1n1.nodisplay=1"));
    assert(!boot_option_skip_display("prefix-m1n1.nodisplay"));
    assert(!boot_option_skip_display("m1n1.nodisplay-suffix"));
    return 0;
}
