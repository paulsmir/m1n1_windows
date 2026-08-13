/* SPDX-License-Identifier: MIT */

#ifndef HV_LAUNCH_GOLDEN_J313_H
#define HV_LAUNCH_GOLDEN_J313_H

#include "hv_launch_preflight.h"

/* Blocking subset captured from the clean eight-core assisted launch on J313.
 * This is intentionally structured data rather than a copied runtime blob so
 * reviewers can see every launch invariant that gates autonomous entry. */
bool hv_launch_golden_j313_init(
    struct hv_contract_snapshot out[HV_LAUNCH_PREFLIGHT_BLOCKING_CHECKPOINTS],
    bool apple_input_declared);

#endif
