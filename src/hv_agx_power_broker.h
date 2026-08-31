/* SPDX-License-Identifier: MIT */

#ifndef HV_AGX_POWER_BROKER_H
#define HV_AGX_POWER_BROKER_H

#include <stdbool.h>
#include <stdint.h>

#include "hv_agx_g2.generated.h"

#define HV_AGX_POWER_MAGIC HV_AGX_G2_POWER_MAGIC
#define HV_AGX_POWER_ABI_VERSION HV_AGX_G2_POWER_ABI_VERSION
#define HV_AGX_POWER_CAP_FIXED_J313_DOMAINS HV_AGX_G2_POWER_CAP_FIXED_J313_DOMAINS

#define HV_AGX_POWER_REG_MAGIC HV_AGX_G2_POWER_REG_MAGIC
#define HV_AGX_POWER_REG_ABI_VERSION HV_AGX_G2_POWER_REG_ABI_VERSION
#define HV_AGX_POWER_REG_CAPABILITIES HV_AGX_G2_POWER_REG_CAPABILITIES
#define HV_AGX_POWER_REG_STATE HV_AGX_G2_POWER_REG_STATE
#define HV_AGX_POWER_REG_RESULT HV_AGX_G2_POWER_REG_RESULT
#define HV_AGX_POWER_REG_RECEIPT_SEQUENCE HV_AGX_G2_POWER_REG_RECEIPT_SEQUENCE
#define HV_AGX_POWER_REG_ACCEPTED_REQUESTS HV_AGX_G2_POWER_REG_ACCEPTED_REQUESTS
#define HV_AGX_POWER_REG_REJECTED_REQUESTS HV_AGX_G2_POWER_REG_REJECTED_REQUESTS
#define HV_AGX_POWER_REG_REQUEST_SEQUENCE HV_AGX_G2_POWER_REG_REQUEST_SEQUENCE
#define HV_AGX_POWER_REG_COMMAND HV_AGX_G2_POWER_REG_COMMAND

enum hv_agx_power_command {
    HV_AGX_POWER_CMD_QUERY = HV_AGX_G2_POWER_CMD_QUERY,
    HV_AGX_POWER_CMD_ON = HV_AGX_G2_POWER_CMD_ON,
    HV_AGX_POWER_CMD_OFF = HV_AGX_G2_POWER_CMD_OFF,
};

enum hv_agx_power_state {
    HV_AGX_POWER_OFF = 0,
    HV_AGX_POWER_ENABLING_ASC,
    HV_AGX_POWER_ENABLING_SGX,
    HV_AGX_POWER_ON,
    HV_AGX_POWER_DISABLING_SGX,
    HV_AGX_POWER_DISABLING_ASC,
    HV_AGX_POWER_FAILED,
};

enum hv_agx_power_result {
    HV_AGX_POWER_RESULT_OK = 0,
    HV_AGX_POWER_RESULT_INVALID_COMMAND,
    HV_AGX_POWER_RESULT_STALE_SEQUENCE,
    HV_AGX_POWER_RESULT_BUSY,
    HV_AGX_POWER_RESULT_TRANSITION_FAILED,
};

struct hv_agx_power_ops {
    bool (*asc_on)(void *opaque);
    bool (*sgx_on)(void *opaque);
    bool (*sgx_off)(void *opaque);
    bool (*asc_off)(void *opaque);
};

struct hv_agx_power_snapshot {
    uint32_t magic;
    uint32_t abi_version;
    uint32_t capabilities;
    uint32_t state;
    uint32_t result;
    uint32_t reserved;
    uint64_t receipt_sequence;
    uint64_t accepted_requests;
    uint64_t rejected_requests;
};

struct hv_agx_power_broker {
    struct hv_agx_power_ops ops;
    void *opaque;
    enum hv_agx_power_state state;
    enum hv_agx_power_result result;
    uint64_t receipt_sequence;
    uint64_t accepted_requests;
    uint64_t rejected_requests;
    uint64_t pending_sequence;
};

void hv_agx_power_broker_init(struct hv_agx_power_broker *broker,
                              const struct hv_agx_power_ops *ops, void *opaque);
bool hv_agx_power_broker_command(struct hv_agx_power_broker *broker, uint32_t command,
                                 uint64_t request_sequence);
void hv_agx_power_broker_snapshot(const struct hv_agx_power_broker *broker,
                                  struct hv_agx_power_snapshot *snapshot);
bool hv_agx_power_broker_mmio(struct hv_agx_power_broker *broker, uint64_t offset,
                              uint64_t *value, bool write, unsigned width);
const struct hv_agx_power_ops *hv_agx_power_j313_ops(void);
bool hv_agx_g2_resources_map(void);
void hv_agx_scanout_service_run_once(void);

#endif
