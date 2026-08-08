/* SPDX-License-Identifier: MIT */

#include "hv_launch_preflight.h"

#include "string.h"

bool hv_launch_preflight_init(struct hv_launch_preflight *state,
                              const struct hv_contract_snapshot *golden, size_t golden_count,
                              const struct hv_contract_schema *schema)
{
    if (!state || !golden || !schema || schema->version != HV_CONTRACT_VERSION ||
        golden_count != HV_LAUNCH_PREFLIGHT_BLOCKING_CHECKPOINTS)
        return false;

    memset(state, 0, sizeof(*state));
    state->golden = golden;
    state->golden_count = golden_count;
    state->schema = schema;
    for (size_t i = 0; i < golden_count; i++) {
        if (golden[i].header.checkpoint != i || golden[i].header.sequence != i + 1)
            return false;
    }
    return true;
}

bool hv_launch_preflight_check_schema(struct hv_launch_preflight *state,
                                      const struct hv_contract_snapshot *actual,
                                      const struct hv_contract_schema *schema)
{
    if (!state || !schema || schema->version != HV_CONTRACT_VERSION || state->blocked ||
        state->next >= state->golden_count || !actual ||
        actual->header.checkpoint != state->next || actual->header.sequence != state->next + 1) {
        if (state)
            state->blocked = true;
        return false;
    }

    if (!hv_contract_compare(&state->golden[state->next], actual, schema,
                             &state->failure)) {
        state->blocked = true;
        return false;
    }
    state->next++;
    return true;
}

bool hv_launch_preflight_check(struct hv_launch_preflight *state,
                               const struct hv_contract_snapshot *actual)
{
    if (!state)
        return false;
    return hv_launch_preflight_check_schema(state, actual, state->schema);
}

bool hv_launch_preflight_enter(const struct hv_launch_preflight *state,
                               hv_launch_preflight_entry_fn entry, void *opaque)
{
    if (!state || !entry || state->blocked || state->next != state->golden_count)
        return false;
    return entry(opaque);
}
