/* SPDX-License-Identifier: MIT */

#ifndef HV_LAUNCH_J313_H
#define HV_LAUNCH_J313_H

#include "hv_launch_snapshot.h"

#define HV_J313_TARGET          0x3331334aU /* J313 */
#define HV_J313_SCHEMA_REVISION 1

#define HV_J313_HACR_REQUIRED_MASK                                                                 \
    ((1ULL << 14) | (1ULL << 16) | (1ULL << 48) | (1ULL << 49) | (1ULL << 50) | (1ULL << 52) |     \
     (1ULL << 56) | (1ULL << 57))
#define HV_J313_MDCR_REQUIRED_MASK  ((1ULL << 8) | (1ULL << 9) | (1ULL << 10) | (1ULL << 11))
#define HV_J313_MDSCR_REQUIRED_MASK (1ULL << 15)
#define HV_J313_AMX_REQUIRED_MASK   (1ULL << 62)
#define HV_J313_ACTLR_REQUIRED_MASK (1ULL << 12)

struct hv_launch_j313_host_state {
    struct hv_contract_identity identity;
    struct hv_contract_boot boot;
    uint64_t adt_size;
    uint8_t adt_digest[HV_CONTRACT_DIGEST_SIZE];
    uint32_t region_count;
    uint32_t mapping_count;
    uint32_t cpu_count;
    uint32_t irq_route_count;
    struct hv_contract_region regions[HV_CONTRACT_MAX_REGIONS];
    struct hv_contract_mapping mappings[HV_CONTRACT_MAX_MAPPINGS];
    struct hv_contract_cpu cpus[HV_CONTRACT_MAX_CPUS];
    struct hv_contract_irq_route irq_routes[HV_CONTRACT_MAX_IRQ_ROUTES];
    struct hv_contract_devices devices;
};

struct hv_launch_j313_cpu_registers {
    uint64_t hacr;
    uint64_t mdcr;
    uint64_t mdscr;
    uint64_t amx_config;
    uint64_t apvmkeylo;
    uint64_t apvmkeyhi;
    uint64_t apsts;
    uint64_t actlr;
};

extern const struct hv_contract_schema HV_J313_CONTRACT_SCHEMA;

void hv_launch_j313_provider_init(struct hv_launch_snapshot_provider *provider);
bool hv_launch_j313_set_base_state(const struct hv_launch_j313_host_state *state);
bool hv_launch_j313_fill_cpus(struct hv_launch_j313_host_state *state, const uint64_t *mpidrs,
                              uint32_t cpu_count,
                              const struct hv_launch_j313_cpu_registers *registers);
bool hv_launch_j313_capture(enum hv_contract_checkpoint checkpoint, uint32_t sequence,
                            struct hv_contract_snapshot *out);

#ifdef HV_LAUNCH_J313_HOST_TEST
void hv_launch_j313_host_set_state(const struct hv_launch_j313_host_state *state);
#endif

#endif
