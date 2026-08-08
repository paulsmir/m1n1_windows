/* SPDX-License-Identifier: MIT */

#ifndef HV_LAUNCH_PREFLIGHT_H
#define HV_LAUNCH_PREFLIGHT_H

#include "hv_launch_contract.h"

#define HV_LAUNCH_PREFLIGHT_BLOCKING_CHECKPOINTS 4

typedef bool (*hv_launch_preflight_entry_fn)(void *opaque);

struct hv_launch_preflight {
    const struct hv_contract_snapshot *golden;
    size_t golden_count;
    const struct hv_contract_schema *schema;
    size_t next;
    bool blocked;
    struct hv_contract_failure failure;
};

bool hv_launch_preflight_init(struct hv_launch_preflight *state,
                              const struct hv_contract_snapshot *golden, size_t golden_count,
                              const struct hv_contract_schema *schema);
bool hv_launch_preflight_check(struct hv_launch_preflight *state,
                               const struct hv_contract_snapshot *actual);
bool hv_launch_preflight_check_schema(struct hv_launch_preflight *state,
                                      const struct hv_contract_snapshot *actual,
                                      const struct hv_contract_schema *schema);
bool hv_launch_preflight_enter(const struct hv_launch_preflight *state,
                               hv_launch_preflight_entry_fn entry, void *opaque);

#endif
