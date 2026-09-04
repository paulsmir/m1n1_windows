/* SPDX-License-Identifier: MIT */

#include "hv_agx_config_snapshot.h"

#include <string.h>

#ifdef HV_AGX_CONFIG_SNAPSHOT_HOST_TEST
int adt_path_offset(const void *tree, const char *path);
const void *adt_getprop(const void *tree, int node, const char *name, uint32_t *length);
int adt_getprop_copy(const void *tree, int node, const char *name, void *out, size_t length);
#define ADT_GETPROP(tree, node, name, value)                                              \
    adt_getprop_copy((tree), (node), (name), (value), sizeof(*(value)))
#else
#include "adt.h"
#endif

#define J313_PERF_STATE_COUNT 7u
#define J313_PERF_TABLE_COUNT 1u
#define J313_BASE_PSTATE 1u
#define J313_MAX_PSTATE 6u
#define J313_POWER_SAMPLE_PERIOD_MS 8u
#define J313_GPU_REGION_BASE 0x9fffb8000ULL

static const char *const scalar_property_names[HV_AGX_CONFIG_SCALAR_COUNT] = {
    "gpu-avg-power-filter-tc-ms", "gpu-avg-power-ki-only",
    "gpu-avg-power-kp", "gpu-avg-power-min-duty-cycle",
    "gpu-avg-power-target-filter-tc", "gpu-fast-die0-integral-gain",
    "gpu-fast-die0-prop-tgt-delta", "gpu-fast-die0-proportional-gain",
    "gpu-fast-die0-release-temp", "gpu-perf-boost-ce-step",
    "gpu-perf-boost-min-util", "gpu-perf-filter-drop-threshold",
    "gpu-perf-filter-time-constant", "gpu-perf-filter-time-constant2",
    "gpu-perf-integral-gain", "gpu-perf-integral-gain2",
    "gpu-perf-integral-min-clamp", "gpu-perf-proportional-gain",
    "gpu-perf-proportional-gain2", "gpu-perf-reset-iters",
    "gpu-perf-tgt-utilization", "gpu-ppm-filter-time-constant-ms",
    "gpu-ppm-ki", "gpu-ppm-kp", "gpu-pwr-filter-time-constant",
    "gpu-pwr-integral-gain", "gpu-pwr-integral-min-clamp",
    "gpu-pwr-min-duty-cycle", "gpu-pwr-proportional-gain",
    "gpu-pwr-sample-period-aic-clks", "gpu-idle-off-delay-ms",
    "gpu-fender-idle-off-delay-ms", "gpu-fw-early-wake-timeout-ms",
};

static bool valid_values(const struct hv_agx_config_snapshot *snapshot)
{
    uint32_t previous_frequency = 0;

    if (snapshot->perf_state_count != J313_PERF_STATE_COUNT ||
        snapshot->perf_state_table_count != J313_PERF_TABLE_COUNT ||
        snapshot->base_pstate != J313_BASE_PSTATE ||
        snapshot->max_pstate != J313_MAX_PSTATE ||
        snapshot->power_sample_period_ms != J313_POWER_SAMPLE_PERIOD_MS ||
        snapshot->gpu_region_base != J313_GPU_REGION_BASE ||
        snapshot->scalar_count != HV_AGX_CONFIG_SCALAR_COUNT ||
        snapshot->scalar_reserved != 0 || snapshot->trailing_reserved != 0 ||
        (snapshot->scalar_presence >> HV_AGX_CONFIG_SCALAR_COUNT) != 0)
        return false;

    for (uint32_t i = 0; i < snapshot->perf_state_count; i++) {
        const struct hv_agx_config_perf_state *state = &snapshot->perf_states[i];

        if (!state->voltage_mv ||
            (i > 0 && state->frequency_hz <= previous_frequency))
            return false;
        previous_frequency = state->frequency_hz;
    }
    for (uint32_t i = snapshot->perf_state_count; i < HV_AGX_CONFIG_MAX_PERF_STATES; i++) {
        if (snapshot->perf_states[i].frequency_hz || snapshot->perf_states[i].voltage_mv)
            return false;
    }
    return true;
}

bool hv_agx_config_snapshot_validate(const struct hv_agx_config_snapshot *snapshot)
{
    if (!snapshot || snapshot->magic != HV_AGX_CONFIG_MAGIC ||
        snapshot->abi_version != HV_AGX_CONFIG_ABI_VERSION ||
        snapshot->size != sizeof(*snapshot) || snapshot->flags != HV_AGX_CONFIG_FLAG_VALID ||
        snapshot->reserved != 0)
        return false;
    return valid_values(snapshot);
}

bool hv_agx_config_snapshot_from_adt(const void *tree,
                                     struct hv_agx_config_snapshot *snapshot)
{
    struct hv_agx_config_snapshot candidate = {0};
    const struct hv_agx_config_perf_state *states;
    uint32_t states_length = 0;
    int sgx;

    if (!tree || !snapshot)
        return false;
    sgx = adt_path_offset(tree, "/arm-io/sgx");
    if (sgx < 0)
        return false;

    if (ADT_GETPROP(tree, sgx, "perf-state-count", &candidate.perf_state_count) < 0 ||
        ADT_GETPROP(tree, sgx, "perf-state-table-count",
                    &candidate.perf_state_table_count) < 0 ||
        ADT_GETPROP(tree, sgx, "gpu-perf-base-pstate", &candidate.base_pstate) < 0 ||
        ADT_GETPROP(tree, sgx, "gpu-num-perf-states", &candidate.max_pstate) < 0 ||
        ADT_GETPROP(tree, sgx, "gpu-power-sample-period",
                    &candidate.power_sample_period_ms) < 0 ||
        ADT_GETPROP(tree, sgx, "gpu-region-base", &candidate.gpu_region_base) < 0)
        return false;

    states = adt_getprop(tree, sgx, "perf-states", &states_length);
    if (!states || candidate.perf_state_count > HV_AGX_CONFIG_MAX_PERF_STATES ||
        states_length != candidate.perf_state_count * candidate.perf_state_table_count *
                             sizeof(*states))
        return false;
    memcpy(candidate.perf_states, states,
           candidate.perf_state_count * sizeof(candidate.perf_states[0]));

    candidate.scalar_count = HV_AGX_CONFIG_SCALAR_COUNT;
    for (uint32_t i = 0; i < HV_AGX_CONFIG_SCALAR_COUNT; i++) {
        if (adt_getprop_copy(tree, sgx, scalar_property_names[i],
                             &candidate.scalar_bits[i],
                             sizeof(candidate.scalar_bits[i])) >= 0)
            candidate.scalar_presence |= 1ULL << i;
    }

    candidate.magic = HV_AGX_CONFIG_MAGIC;
    candidate.abi_version = HV_AGX_CONFIG_ABI_VERSION;
    candidate.size = sizeof(candidate);
    candidate.flags = HV_AGX_CONFIG_FLAG_VALID;
    if (!hv_agx_config_snapshot_validate(&candidate))
        return false;

    *snapshot = candidate;
    return true;
}

bool hv_agx_config_snapshot_mmio(const struct hv_agx_config_snapshot *snapshot,
                                 uint64_t offset, uint64_t *value, bool write,
                                 unsigned width)
{
    size_t bytes;

    if (!snapshot || !value || write || !hv_agx_config_snapshot_validate(snapshot))
        return false;
    if (width > 3)
        return false;
    bytes = (size_t)1u << width;
    if ((offset & (bytes - 1)) != 0 || offset > sizeof(*snapshot) - bytes)
        return false;

    *value = 0;
    memcpy(value, (const uint8_t *)snapshot + offset, bytes);
    return true;
}
