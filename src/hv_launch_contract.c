/* SPDX-License-Identifier: MIT */

#include "hv_launch_contract.h"

static uint32_t hv_contract_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = ~0U;

    while (size--) {
        crc ^= *data++;
        for (unsigned int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }

    return ~crc;
}

static void hv_contract_fail(struct hv_contract_failure *failure, uint16_t field, uint16_t index,
                             uint16_t rule, uint64_t expected, uint64_t actual)
{
    if (!failure)
        return;

    failure->field = field;
    failure->index = index;
    failure->rule = rule;
    failure->reserved = 0;
    failure->expected = expected;
    failure->actual = actual;
}

static bool hv_contract_snapshot_valid(const struct hv_contract_snapshot *snapshot,
                                       struct hv_contract_failure *failure)
{
    const uint8_t *payload;
    uint32_t crc;

    if (!snapshot)
        return false;

    if (snapshot->header.magic != HV_CONTRACT_MAGIC) {
        hv_contract_fail(failure, HV_CONTRACT_FIELD_HEADER_MAGIC, 0, HV_CONTRACT_EXACT,
                         HV_CONTRACT_MAGIC, snapshot->header.magic);
        return false;
    }
    if (snapshot->header.version != HV_CONTRACT_VERSION) {
        hv_contract_fail(failure, HV_CONTRACT_FIELD_HEADER_VERSION, 0, HV_CONTRACT_EXACT,
                         HV_CONTRACT_VERSION, snapshot->header.version);
        return false;
    }
    if (snapshot->header.header_size != sizeof(snapshot->header)) {
        hv_contract_fail(failure, HV_CONTRACT_FIELD_HEADER_SIZE, 0, HV_CONTRACT_EXACT,
                         sizeof(snapshot->header), snapshot->header.header_size);
        return false;
    }
    if (snapshot->header.payload_size != sizeof(*snapshot) - sizeof(snapshot->header)) {
        hv_contract_fail(failure, HV_CONTRACT_FIELD_PAYLOAD_SIZE, 0, HV_CONTRACT_EXACT,
                         sizeof(*snapshot) - sizeof(snapshot->header),
                         snapshot->header.payload_size);
        return false;
    }
    if (snapshot->header.checkpoint >= HV_CONTRACT_CHECKPOINT_COUNT) {
        hv_contract_fail(failure, HV_CONTRACT_FIELD_CHECKPOINT, 0, HV_CONTRACT_RANGE,
                         HV_CONTRACT_CHECKPOINT_COUNT - 1, snapshot->header.checkpoint);
        return false;
    }
    if (snapshot->region_count > HV_CONTRACT_MAX_REGIONS ||
        snapshot->cpu_count > HV_CONTRACT_MAX_CPUS)
        return false;

    payload = (const uint8_t *)snapshot + sizeof(snapshot->header);
    crc = hv_contract_crc32(payload, snapshot->header.payload_size);
    if (crc != snapshot->header.payload_crc32) {
        hv_contract_fail(failure, HV_CONTRACT_FIELD_CHECKSUM, 0, HV_CONTRACT_EXACT,
                         snapshot->header.payload_crc32, crc);
        return false;
    }

    return true;
}

bool hv_contract_finalize(struct hv_contract_snapshot *snapshot)
{
    const uint8_t *payload;

    if (!snapshot || snapshot->header.magic != HV_CONTRACT_MAGIC ||
        snapshot->header.version != HV_CONTRACT_VERSION ||
        snapshot->header.checkpoint >= HV_CONTRACT_CHECKPOINT_COUNT ||
        snapshot->region_count > HV_CONTRACT_MAX_REGIONS ||
        snapshot->cpu_count > HV_CONTRACT_MAX_CPUS)
        return false;

    snapshot->header.header_size = sizeof(snapshot->header);
    snapshot->header.payload_size = sizeof(*snapshot) - sizeof(snapshot->header);
    payload = (const uint8_t *)snapshot + sizeof(snapshot->header);
    snapshot->header.payload_crc32 = hv_contract_crc32(payload, snapshot->header.payload_size);
    return true;
}

static bool hv_contract_compare_region(const struct hv_contract_snapshot *golden,
                                       const struct hv_contract_snapshot *actual,
                                       const struct hv_contract_rule *rule,
                                       struct hv_contract_failure *failure)
{
    const struct hv_contract_region *expected;
    const struct hv_contract_region *observed;

    if (rule->index >= golden->region_count || rule->index >= actual->region_count) {
        hv_contract_fail(failure, HV_CONTRACT_FIELD_REGION, rule->index, rule->kind,
                         golden->region_count, actual->region_count);
        return false;
    }

    expected = &golden->regions[rule->index];
    observed = &actual->regions[rule->index];

    if (expected->kind != observed->kind || expected->size != observed->size ||
        expected->flags != observed->flags) {
        hv_contract_fail(failure, HV_CONTRACT_FIELD_REGION, rule->index, rule->kind, expected->size,
                         observed->size);
        return false;
    }

    if (rule->kind == HV_CONTRACT_EXACT) {
        if (expected->base == observed->base)
            return true;
        hv_contract_fail(failure, HV_CONTRACT_FIELD_REGION, rule->index, rule->kind, expected->base,
                         observed->base);
        return false;
    }

    if (rule->kind == HV_CONTRACT_RELATIVE_REGION) {
        const struct hv_contract_region *expected_parent;
        const struct hv_contract_region *observed_parent;
        uint64_t expected_offset;
        uint64_t observed_offset;

        if (rule->reference >= golden->region_count || rule->reference >= actual->region_count) {
            hv_contract_fail(failure, HV_CONTRACT_FIELD_SCHEMA, rule->index, rule->kind,
                             golden->region_count, rule->reference);
            return false;
        }
        expected_parent = &golden->regions[rule->reference];
        observed_parent = &actual->regions[rule->reference];
        if (expected->base < expected_parent->base || observed->base < observed_parent->base ||
            expected->size > expected_parent->size || observed->size > observed_parent->size ||
            expected->base - expected_parent->base > expected_parent->size - expected->size ||
            observed->base - observed_parent->base > observed_parent->size - observed->size)
            return false;
        expected_offset = expected->base - expected_parent->base;
        observed_offset = observed->base - observed_parent->base;
        if (expected_offset == observed_offset)
            return true;
        hv_contract_fail(failure, HV_CONTRACT_FIELD_REGION, rule->index, rule->kind,
                         expected_offset, observed_offset);
        return false;
    }

    hv_contract_fail(failure, HV_CONTRACT_FIELD_SCHEMA, rule->index, rule->kind, 0, rule->kind);
    return false;
}

static bool hv_contract_compare_scalar(uint64_t expected, uint64_t actual,
                                       const struct hv_contract_rule *rule,
                                       struct hv_contract_failure *failure)
{
    bool matches = false;

    switch (rule->kind) {
        case HV_CONTRACT_EXACT:
            matches = expected == actual;
            break;
        case HV_CONTRACT_MASKED:
            matches = (expected & rule->mask) == (actual & rule->mask);
            break;
        case HV_CONTRACT_RANGE:
            matches = actual >= rule->minimum && actual <= rule->maximum;
            break;
        default:
            break;
    }

    if (!matches)
        hv_contract_fail(failure, rule->field, rule->index, rule->kind, expected, actual);
    return matches;
}

static bool hv_contract_compare_mpidr_set(const struct hv_contract_snapshot *golden,
                                          const struct hv_contract_snapshot *actual,
                                          struct hv_contract_failure *failure)
{
    if (golden->cpu_count != actual->cpu_count)
        return false;

    for (uint32_t expected = 0; expected < golden->cpu_count; expected++) {
        bool found = false;

        for (uint32_t observed = 0; observed < actual->cpu_count; observed++) {
            if (golden->cpus[expected].mpidr == actual->cpus[observed].mpidr) {
                found = true;
                break;
            }
        }
        if (!found) {
            hv_contract_fail(failure, HV_CONTRACT_FIELD_CPU_MPIDR, expected, HV_CONTRACT_SET,
                             golden->cpus[expected].mpidr, 0);
            return false;
        }
    }
    return true;
}

static bool hv_contract_compare_rule(const struct hv_contract_snapshot *golden,
                                     const struct hv_contract_snapshot *actual,
                                     const struct hv_contract_rule *rule,
                                     struct hv_contract_failure *failure)
{
    switch (rule->field) {
        case HV_CONTRACT_FIELD_REGION:
            return hv_contract_compare_region(golden, actual, rule, failure);
        case HV_CONTRACT_FIELD_CPU_ACTLR:
            if (rule->index >= golden->cpu_count || rule->index >= actual->cpu_count) {
                hv_contract_fail(failure, HV_CONTRACT_FIELD_SCHEMA, rule->index, rule->kind,
                                 golden->cpu_count, rule->index);
                return false;
            }
            return hv_contract_compare_scalar(golden->cpus[rule->index].actlr,
                                              actual->cpus[rule->index].actlr, rule, failure);
        case HV_CONTRACT_FIELD_CPU_MPIDR:
            if (rule->kind == HV_CONTRACT_SET)
                return hv_contract_compare_mpidr_set(golden, actual, failure);
            if (rule->index >= golden->cpu_count || rule->index >= actual->cpu_count) {
                hv_contract_fail(failure, HV_CONTRACT_FIELD_SCHEMA, rule->index, rule->kind,
                                 golden->cpu_count, rule->index);
                return false;
            }
            return hv_contract_compare_scalar(golden->cpus[rule->index].mpidr,
                                              actual->cpus[rule->index].mpidr, rule, failure);
        case HV_CONTRACT_FIELD_ADT_DIGEST:
            if (rule->kind != HV_CONTRACT_DIGEST) {
                hv_contract_fail(failure, HV_CONTRACT_FIELD_SCHEMA, rule->index, rule->kind,
                                 HV_CONTRACT_DIGEST, rule->kind);
                return false;
            }
            if (golden->adt_size != actual->adt_size) {
                hv_contract_fail(failure, rule->field, rule->index, rule->kind, golden->adt_size,
                                 actual->adt_size);
                return false;
            }
            for (size_t i = 0; i < HV_CONTRACT_DIGEST_SIZE; i++) {
                if (golden->adt_digest[i] != actual->adt_digest[i]) {
                    hv_contract_fail(failure, rule->field, i, rule->kind, golden->adt_digest[i],
                                     actual->adt_digest[i]);
                    return false;
                }
            }
            return true;
        default:
            hv_contract_fail(failure, HV_CONTRACT_FIELD_SCHEMA, rule->index, rule->kind, 0,
                             rule->field);
            return false;
    }
}

bool hv_contract_compare(const struct hv_contract_snapshot *golden,
                         const struct hv_contract_snapshot *actual,
                         const struct hv_contract_schema *schema,
                         struct hv_contract_failure *failure)
{
    if (failure)
        *failure = (struct hv_contract_failure){0};
    if (!schema || schema->version != HV_CONTRACT_VERSION ||
        (schema->rule_count && !schema->rules)) {
        hv_contract_fail(failure, HV_CONTRACT_FIELD_SCHEMA, 0, HV_CONTRACT_EXACT,
                         HV_CONTRACT_VERSION, schema ? schema->version : 0);
        return false;
    }
    if (!hv_contract_snapshot_valid(golden, failure) ||
        !hv_contract_snapshot_valid(actual, failure))
        return false;
    if (golden->header.checkpoint != actual->header.checkpoint) {
        hv_contract_fail(failure, HV_CONTRACT_FIELD_CHECKPOINT, 0, HV_CONTRACT_EXACT,
                         golden->header.checkpoint, actual->header.checkpoint);
        return false;
    }
    if (golden->header.sequence != actual->header.sequence) {
        hv_contract_fail(failure, HV_CONTRACT_FIELD_SEQUENCE, 0, HV_CONTRACT_EXACT,
                         golden->header.sequence, actual->header.sequence);
        return false;
    }

    for (size_t i = 0; i < schema->rule_count; i++) {
        if (!hv_contract_compare_rule(golden, actual, &schema->rules[i], failure))
            return false;
    }
    return true;
}
