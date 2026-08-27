/* SPDX-License-Identifier: MIT */

#ifndef HV_AGX_CONFIG_SNAPSHOT_H
#define HV_AGX_CONFIG_SNAPSHOT_H

#include <stdbool.h>
#include <stdint.h>

#define HV_AGX_CONFIG_MAGIC 0x43475841u /* "AGXC" */
#define HV_AGX_CONFIG_ABI_VERSION 1u
#define HV_AGX_CONFIG_MAX_PERF_STATES 16u
#define HV_AGX_CONFIG_FLAG_VALID 0x1u
#define HV_AGX_CONFIG_MMIO_OFFSET 0x100u

struct hv_agx_config_perf_state {
    uint32_t frequency_hz;
    uint32_t voltage_mv;
};

/* Immutable boot configuration copied from /arm-io/sgx before guest entry. */
struct hv_agx_config_snapshot {
    uint32_t magic;
    uint32_t abi_version;
    uint32_t size;
    uint32_t flags;
    uint32_t perf_state_count;
    uint32_t perf_state_table_count;
    uint32_t base_pstate;
    uint32_t max_pstate;
    uint32_t power_sample_period_ms;
    uint32_t reserved;
    uint64_t gpu_region_base;
    struct hv_agx_config_perf_state perf_states[HV_AGX_CONFIG_MAX_PERF_STATES];
};

bool hv_agx_config_snapshot_from_adt(const void *tree,
                                     struct hv_agx_config_snapshot *snapshot);
bool hv_agx_config_snapshot_validate(const struct hv_agx_config_snapshot *snapshot);
bool hv_agx_config_snapshot_mmio(const struct hv_agx_config_snapshot *snapshot,
                                 uint64_t offset, uint64_t *value, bool write,
                                 unsigned width);

#endif
