/* SPDX-License-Identifier: MIT */

#ifndef CPUFREQ_H
#define CPUFREQ_H

int cpufreq_init(void);
void cpufreq_fixup(void);
void cpufreq_print_state(const char *phase);

#endif
