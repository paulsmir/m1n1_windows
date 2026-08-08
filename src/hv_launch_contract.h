/* SPDX-License-Identifier: MIT */

#ifndef HV_LAUNCH_CONTRACT_H
#define HV_LAUNCH_CONTRACT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HV_CONTRACT_MAGIC       0x4a43314cU /* "L1CJ" on the wire */
#define HV_CONTRACT_VERSION     1
#define HV_CONTRACT_MAX_CPUS    8
#define HV_CONTRACT_MAX_REGIONS 16
#define HV_CONTRACT_DIGEST_SIZE 32

enum hv_contract_checkpoint {
    HV_CONTRACT_PRE_HV_INIT,
    HV_CONTRACT_POST_HV_INIT,
    HV_CONTRACT_POST_MAPS,
    HV_CONTRACT_PRE_GUEST,
    HV_CONTRACT_CPU_ENTRY,
    HV_CONTRACT_CHECKPOINT_COUNT,
};

enum hv_contract_rule_kind {
    HV_CONTRACT_EXACT,
    HV_CONTRACT_MASKED,
    HV_CONTRACT_RELATIVE_REGION,
    HV_CONTRACT_SET,
    HV_CONTRACT_DIGEST,
    HV_CONTRACT_RANGE,
};

enum hv_contract_region_kind {
    HV_CONTRACT_REGION_GUEST_RAM,
    HV_CONTRACT_REGION_HEAP,
    HV_CONTRACT_REGION_FIRMWARE,
    HV_CONTRACT_REGION_ADT,
    HV_CONTRACT_REGION_BOOT_ARGS,
    HV_CONTRACT_REGION_FRAMEBUFFER,
    HV_CONTRACT_REGION_LOW_MEMORY,
};

enum hv_contract_field {
    HV_CONTRACT_FIELD_NONE,
    HV_CONTRACT_FIELD_HEADER_MAGIC,
    HV_CONTRACT_FIELD_HEADER_VERSION,
    HV_CONTRACT_FIELD_HEADER_SIZE,
    HV_CONTRACT_FIELD_PAYLOAD_SIZE,
    HV_CONTRACT_FIELD_CHECKPOINT,
    HV_CONTRACT_FIELD_SEQUENCE,
    HV_CONTRACT_FIELD_CHECKSUM,
    HV_CONTRACT_FIELD_SCHEMA,
    HV_CONTRACT_FIELD_REGION,
    HV_CONTRACT_FIELD_CPU_ACTLR,
    HV_CONTRACT_FIELD_CPU_MPIDR,
    HV_CONTRACT_FIELD_ADT_DIGEST,
};

struct hv_contract_header {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t payload_size;
    uint32_t checkpoint;
    uint32_t sequence;
    uint32_t payload_crc32;
} __attribute__((packed));

struct hv_contract_region {
    uint32_t kind;
    uint32_t flags;
    uint64_t base;
    uint64_t size;
} __attribute__((packed));

struct hv_contract_cpu {
    uint64_t mpidr;
    uint64_t actlr;
} __attribute__((packed));

struct hv_contract_snapshot {
    struct hv_contract_header header;
    uint32_t region_count;
    uint32_t cpu_count;
    uint64_t adt_size;
    uint8_t adt_digest[HV_CONTRACT_DIGEST_SIZE];
    struct hv_contract_region regions[HV_CONTRACT_MAX_REGIONS];
    struct hv_contract_cpu cpus[HV_CONTRACT_MAX_CPUS];
} __attribute__((packed));

struct hv_contract_rule {
    uint16_t field;
    uint16_t index;
    uint16_t kind;
    uint16_t reference;
    uint64_t mask;
    uint64_t minimum;
    uint64_t maximum;
};

struct hv_contract_schema {
    uint16_t version;
    const struct hv_contract_rule *rules;
    size_t rule_count;
};

struct hv_contract_failure {
    uint16_t field;
    uint16_t index;
    uint16_t rule;
    uint16_t reserved;
    uint64_t expected;
    uint64_t actual;
};

bool hv_contract_finalize(struct hv_contract_snapshot *snapshot);
bool hv_contract_compare(const struct hv_contract_snapshot *golden,
                         const struct hv_contract_snapshot *actual,
                         const struct hv_contract_schema *schema,
                         struct hv_contract_failure *failure);

#endif
