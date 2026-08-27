#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../src/hv_agx_config_snapshot.h"

struct perf_state {
    uint32_t frequency_hz;
    uint32_t voltage_uv;
};

static uint32_t perf_state_count = 7;
static uint32_t table_count = 1;
static uint32_t base_pstate = 1;
static uint32_t max_pstate = 6;
static uint32_t sample_period = 8;
static uint64_t gpu_region_base = 0x9fffb8000ULL;
static struct perf_state states[7] = {
    {300000000, 650000}, {450000000, 675000}, {600000000, 700000},
    {750000000, 725000}, {900000000, 750000}, {1050000000, 775000},
    {1278000000, 800000},
};
static bool omit_perf_states;
static uint32_t perf_states_length = sizeof(states);

int adt_path_offset(const void *tree, const char *path)
{
    (void)tree;
    return strcmp(path, "/arm-io/sgx") == 0 ? 7 : -1;
}

const void *adt_getprop(const void *tree, int node, const char *name, uint32_t *length)
{
    (void)tree;
    assert(node == 7);
    if (strcmp(name, "perf-states") != 0 || omit_perf_states)
        return NULL;
    if (length)
        *length = perf_states_length;
    return states;
}

int adt_getprop_copy(const void *tree, int node, const char *name, void *out, size_t length)
{
    (void)tree;
    assert(node == 7);
#define COPY_PROP(prop_name, value)                                                           \
    if (strcmp(name, prop_name) == 0 && length == sizeof(value)) {                             \
        memcpy(out, &(value), sizeof(value));                                                   \
        return (int)sizeof(value);                                                              \
    }
    COPY_PROP("perf-state-count", perf_state_count)
    COPY_PROP("perf-state-table-count", table_count)
    COPY_PROP("gpu-perf-base-pstate", base_pstate)
    COPY_PROP("gpu-num-perf-states", max_pstate)
    COPY_PROP("gpu-power-sample-period", sample_period)
    COPY_PROP("gpu-region-base", gpu_region_base)
#undef COPY_PROP
    return -1;
}

static void reset_fixture(void)
{
    perf_state_count = 7;
    table_count = 1;
    base_pstate = 1;
    max_pstate = 6;
    sample_period = 8;
    gpu_region_base = 0x9fffb8000ULL;
    omit_perf_states = false;
    perf_states_length = sizeof(states);
}

static void test_exact_j313_snapshot(void)
{
    struct hv_agx_config_snapshot snapshot;

    reset_fixture();
    memset(&snapshot, 0xa5, sizeof(snapshot));
    assert(hv_agx_config_snapshot_from_adt((void *)1, &snapshot));
    assert(hv_agx_config_snapshot_validate(&snapshot));
    assert(snapshot.magic == HV_AGX_CONFIG_MAGIC);
    assert(snapshot.abi_version == HV_AGX_CONFIG_ABI_VERSION);
    assert(snapshot.size == sizeof(snapshot));
    assert(snapshot.flags == HV_AGX_CONFIG_FLAG_VALID);
    assert(snapshot.perf_state_count == 7);
    assert(snapshot.perf_states[6].frequency_hz == 1278000000);
    assert(snapshot.perf_states[6].voltage_uv == 800000);
    assert(snapshot.perf_states[7].frequency_hz == 0);
}

static void test_rejects_missing_or_wrong_sized_table_without_partial_output(void)
{
    struct hv_agx_config_snapshot snapshot;
    struct hv_agx_config_snapshot before;

    reset_fixture();
    memset(&snapshot, 0x5a, sizeof(snapshot));
    before = snapshot;
    omit_perf_states = true;
    assert(!hv_agx_config_snapshot_from_adt((void *)1, &snapshot));
    assert(memcmp(&snapshot, &before, sizeof(snapshot)) == 0);

    reset_fixture();
    perf_states_length--;
    assert(!hv_agx_config_snapshot_from_adt((void *)1, &snapshot));
    assert(memcmp(&snapshot, &before, sizeof(snapshot)) == 0);
}

static void test_rejects_non_j313_geometry_and_invalid_values(void)
{
    struct hv_agx_config_snapshot snapshot;

    reset_fixture();
    table_count = 2;
    assert(!hv_agx_config_snapshot_from_adt((void *)1, &snapshot));
    reset_fixture();
    base_pstate = 7;
    assert(!hv_agx_config_snapshot_from_adt((void *)1, &snapshot));
    reset_fixture();
    states[2].frequency_hz = 0;
    assert(!hv_agx_config_snapshot_from_adt((void *)1, &snapshot));
    states[2].frequency_hz = 600000000;
    reset_fixture();
    gpu_region_base = 0;
    assert(!hv_agx_config_snapshot_from_adt((void *)1, &snapshot));
}

static void test_validator_rejects_mutation(void)
{
    struct hv_agx_config_snapshot snapshot;

    reset_fixture();
    assert(hv_agx_config_snapshot_from_adt((void *)1, &snapshot));
    snapshot.flags = 0;
    assert(!hv_agx_config_snapshot_validate(&snapshot));
}

static void test_mmio_is_read_only_bounded_and_exact(void)
{
    struct hv_agx_config_snapshot snapshot;
    uint64_t value = 0;

    reset_fixture();
    assert(hv_agx_config_snapshot_from_adt((void *)1, &snapshot));
    assert(hv_agx_config_snapshot_mmio(&snapshot, 0, &value, false, 2));
    assert(value == HV_AGX_CONFIG_MAGIC);
    assert(hv_agx_config_snapshot_mmio(&snapshot, 0x28, &value, false, 3));
    assert(value == 0x9fffb8000ULL);
    assert(hv_agx_config_snapshot_mmio(&snapshot, 0x30, &value, false, 3));
    assert((uint32_t)value == 300000000);
    assert((uint32_t)(value >> 32) == 650000);
    assert(!hv_agx_config_snapshot_mmio(&snapshot, 0, &value, true, 2));
    assert(!hv_agx_config_snapshot_mmio(&snapshot, 2, &value, false, 2));
    assert(!hv_agx_config_snapshot_mmio(&snapshot, sizeof(snapshot), &value, false, 2));
    snapshot.flags = 0;
    assert(!hv_agx_config_snapshot_mmio(&snapshot, 0, &value, false, 2));
}

int main(void)
{
    test_exact_j313_snapshot();
    test_rejects_missing_or_wrong_sized_table_without_partial_output();
    test_rejects_non_j313_geometry_and_invalid_values();
    test_validator_rejects_mutation();
    test_mmio_is_read_only_bounded_and_exact();
    return 0;
}
