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
#define HV_AGX_CONFIG_SCALAR_COUNT 33u

enum hv_agx_config_scalar {
    HV_AGX_SCALAR_AVG_POWER_FILTER_TC_MS,
    HV_AGX_SCALAR_AVG_POWER_KI_ONLY,
    HV_AGX_SCALAR_AVG_POWER_KP,
    HV_AGX_SCALAR_AVG_POWER_MIN_DUTY_CYCLE,
    HV_AGX_SCALAR_AVG_POWER_TARGET_FILTER_TC,
    HV_AGX_SCALAR_FAST_DIE0_INTEGRAL_GAIN,
    HV_AGX_SCALAR_FAST_DIE0_PROP_TGT_DELTA,
    HV_AGX_SCALAR_FAST_DIE0_PROPORTIONAL_GAIN,
    HV_AGX_SCALAR_FAST_DIE0_RELEASE_TEMP,
    HV_AGX_SCALAR_PERF_BOOST_CE_STEP,
    HV_AGX_SCALAR_PERF_BOOST_MIN_UTIL,
    HV_AGX_SCALAR_PERF_FILTER_DROP_THRESHOLD,
    HV_AGX_SCALAR_PERF_FILTER_TIME_CONSTANT,
    HV_AGX_SCALAR_PERF_FILTER_TIME_CONSTANT2,
    HV_AGX_SCALAR_PERF_INTEGRAL_GAIN,
    HV_AGX_SCALAR_PERF_INTEGRAL_GAIN2,
    HV_AGX_SCALAR_PERF_INTEGRAL_MIN_CLAMP,
    HV_AGX_SCALAR_PERF_PROPORTIONAL_GAIN,
    HV_AGX_SCALAR_PERF_PROPORTIONAL_GAIN2,
    HV_AGX_SCALAR_PERF_RESET_ITERS,
    HV_AGX_SCALAR_PERF_TGT_UTILIZATION,
    HV_AGX_SCALAR_PPM_FILTER_TIME_CONSTANT_MS,
    HV_AGX_SCALAR_PPM_KI,
    HV_AGX_SCALAR_PPM_KP,
    HV_AGX_SCALAR_PWR_FILTER_TIME_CONSTANT,
    HV_AGX_SCALAR_PWR_INTEGRAL_GAIN,
    HV_AGX_SCALAR_PWR_INTEGRAL_MIN_CLAMP,
    HV_AGX_SCALAR_PWR_MIN_DUTY_CYCLE,
    HV_AGX_SCALAR_PWR_PROPORTIONAL_GAIN,
    HV_AGX_SCALAR_PWR_SAMPLE_PERIOD_AIC_CLKS,
    HV_AGX_SCALAR_IDLE_OFF_DELAY_MS,
    HV_AGX_SCALAR_FENDER_IDLE_OFF_DELAY_MS,
    HV_AGX_SCALAR_FW_EARLY_WAKE_TIMEOUT_MS,
};

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
    uint32_t scalar_count;
    uint32_t scalar_reserved;
    uint64_t scalar_presence;
    uint32_t scalar_bits[HV_AGX_CONFIG_SCALAR_COUNT];
    uint32_t trailing_reserved;
};

bool hv_agx_config_snapshot_from_adt(const void *tree,
                                     struct hv_agx_config_snapshot *snapshot);
bool hv_agx_config_snapshot_validate(const struct hv_agx_config_snapshot *snapshot);
bool hv_agx_config_snapshot_mmio(const struct hv_agx_config_snapshot *snapshot,
                                 uint64_t offset, uint64_t *value, bool write,
                                 unsigned width);

#endif
