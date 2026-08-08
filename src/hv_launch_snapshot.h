/* SPDX-License-Identifier: MIT */

#ifndef HV_LAUNCH_SNAPSHOT_H
#define HV_LAUNCH_SNAPSHOT_H

#include "hv_launch_contract.h"

struct hv_launch_snapshot_provider {
    void *context;
    bool (*read_identity)(void *context, struct hv_contract_identity *out);
    bool (*read_boot)(void *context, struct hv_contract_boot *out);
    bool (*read_adt)(void *context, uint64_t *size, uint8_t digest[HV_CONTRACT_DIGEST_SIZE]);
    bool (*read_regions)(void *context, struct hv_contract_region *out, uint32_t capacity,
                         uint32_t *count);
    bool (*read_mappings)(void *context, struct hv_contract_mapping *out, uint32_t capacity,
                          uint32_t *count);
    bool (*read_cpus)(void *context, struct hv_contract_cpu *out, uint32_t capacity,
                      uint32_t *count);
    bool (*read_irq_routes)(void *context, struct hv_contract_irq_route *out, uint32_t capacity,
                            uint32_t *count);
    bool (*read_devices)(void *context, struct hv_contract_devices *out);
};

bool hv_launch_snapshot_collect(enum hv_contract_checkpoint checkpoint, uint32_t sequence,
                                const struct hv_launch_snapshot_provider *provider,
                                struct hv_contract_snapshot *out);

#endif
