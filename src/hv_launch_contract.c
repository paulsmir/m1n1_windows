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

static const struct hv_contract_cpu *
hv_contract_find_cpu(const struct hv_contract_snapshot *snapshot, uint64_t mpidr)
{
    for (uint32_t i = 0; i < snapshot->cpu_count; i++) {
        if (snapshot->cpus[i].mpidr == mpidr)
            return &snapshot->cpus[i];
    }
    return NULL;
}

static bool hv_contract_cpu_value(const struct hv_contract_cpu *cpu, uint16_t field,
                                  uint64_t *value)
{
    switch (field) {
        case HV_CONTRACT_FIELD_CPU_MPIDR:
            *value = cpu->mpidr;
            return true;
        case HV_CONTRACT_FIELD_CPU_HACR:
            *value = cpu->hacr;
            return true;
        case HV_CONTRACT_FIELD_CPU_MDCR:
            *value = cpu->mdcr;
            return true;
        case HV_CONTRACT_FIELD_CPU_MDSCR:
            *value = cpu->mdscr;
            return true;
        case HV_CONTRACT_FIELD_CPU_AMX_CONFIG:
            *value = cpu->amx_config;
            return true;
        case HV_CONTRACT_FIELD_CPU_APVMKEYLO:
            *value = cpu->apvmkeylo;
            return true;
        case HV_CONTRACT_FIELD_CPU_APVMKEYHI:
            *value = cpu->apvmkeyhi;
            return true;
        case HV_CONTRACT_FIELD_CPU_APSTS:
            *value = cpu->apsts;
            return true;
        case HV_CONTRACT_FIELD_CPU_ACTLR:
            *value = cpu->actlr;
            return true;
        default:
            return false;
    }
}

static bool hv_contract_compare_cpu_rule(const struct hv_contract_snapshot *golden,
                                         const struct hv_contract_snapshot *actual,
                                         const struct hv_contract_rule *rule,
                                         struct hv_contract_failure *failure)
{
    uint32_t first = rule->index;
    uint32_t end = first + 1;

    if (rule->field == HV_CONTRACT_FIELD_CPU_MPIDR && rule->kind == HV_CONTRACT_SET)
        return hv_contract_compare_mpidr_set(golden, actual, failure);

    if (rule->index == HV_CONTRACT_ALL_ITEMS) {
        first = 0;
        end = golden->cpu_count;
    } else if (rule->index >= golden->cpu_count) {
        hv_contract_fail(failure, HV_CONTRACT_FIELD_SCHEMA, rule->index, rule->kind,
                         golden->cpu_count, rule->index);
        return false;
    }

    for (uint32_t i = first; i < end; i++) {
        const struct hv_contract_cpu *expected_cpu = &golden->cpus[i];
        const struct hv_contract_cpu *observed_cpu =
            hv_contract_find_cpu(actual, expected_cpu->mpidr);
        uint64_t expected = 0;
        uint64_t observed = 0;
        struct hv_contract_rule indexed_rule = *rule;

        if (!observed_cpu || !hv_contract_cpu_value(expected_cpu, rule->field, &expected) ||
            !hv_contract_cpu_value(observed_cpu, rule->field, &observed)) {
            hv_contract_fail(failure, rule->field, i, rule->kind, expected_cpu->mpidr,
                             observed_cpu ? observed_cpu->mpidr : UINT64_MAX);
            return false;
        }
        indexed_rule.index = i;
        if (!hv_contract_compare_scalar(expected, observed, &indexed_rule, failure))
            return false;
    }
    return true;
}

static bool hv_contract_compare_irq_set(const struct hv_contract_snapshot *golden,
                                        const struct hv_contract_snapshot *actual,
                                        struct hv_contract_failure *failure)
{
    if (golden->irq_route_count != actual->irq_route_count) {
        hv_contract_fail(failure, HV_CONTRACT_FIELD_IRQ_ROUTE, HV_CONTRACT_ALL_ITEMS,
                         HV_CONTRACT_SET, golden->irq_route_count, actual->irq_route_count);
        return false;
    }

    for (uint32_t expected = 0; expected < golden->irq_route_count; expected++) {
        bool found = false;

        for (uint32_t observed = 0; observed < actual->irq_route_count; observed++) {
            const struct hv_contract_irq_route *left = &golden->irq_routes[expected];
            const struct hv_contract_irq_route *right = &actual->irq_routes[observed];

            if (left->physical_irq == right->physical_irq && left->vintid == right->vintid &&
                left->flags == right->flags && left->device == right->device) {
                found = true;
                break;
            }
        }
        if (!found) {
            hv_contract_fail(failure, HV_CONTRACT_FIELD_IRQ_ROUTE, expected, HV_CONTRACT_SET,
                             golden->irq_routes[expected].vintid, UINT64_MAX);
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
        case HV_CONTRACT_FIELD_IDENTITY_TARGET:
            return hv_contract_compare_scalar(golden->identity.target, actual->identity.target,
                                              rule, failure);
        case HV_CONTRACT_FIELD_BOOT_RAM_BASE:
            return hv_contract_compare_scalar(golden->boot.ram_base, actual->boot.ram_base, rule,
                                              failure);
        case HV_CONTRACT_FIELD_BOOT_RAM_SIZE:
            return hv_contract_compare_scalar(golden->boot.ram_size, actual->boot.ram_size, rule,
                                              failure);
        case HV_CONTRACT_FIELD_BOOT_GUEST_ENTRY:
            return hv_contract_compare_scalar(golden->boot.guest_entry, actual->boot.guest_entry,
                                              rule, failure);
        case HV_CONTRACT_FIELD_BOOT_ARG:
            if (rule->index >= 4) {
                hv_contract_fail(failure, HV_CONTRACT_FIELD_SCHEMA, rule->index, rule->kind, 4,
                                 rule->index);
                return false;
            }
            return hv_contract_compare_scalar(golden->boot.args[rule->index],
                                              actual->boot.args[rule->index], rule, failure);
        case HV_CONTRACT_FIELD_REGION:
            return hv_contract_compare_region(golden, actual, rule, failure);
        case HV_CONTRACT_FIELD_CPU_ACTLR:
        case HV_CONTRACT_FIELD_CPU_MPIDR:
        case HV_CONTRACT_FIELD_CPU_HACR:
        case HV_CONTRACT_FIELD_CPU_MDCR:
        case HV_CONTRACT_FIELD_CPU_MDSCR:
        case HV_CONTRACT_FIELD_CPU_AMX_CONFIG:
        case HV_CONTRACT_FIELD_CPU_APVMKEYLO:
        case HV_CONTRACT_FIELD_CPU_APVMKEYHI:
        case HV_CONTRACT_FIELD_CPU_APSTS:
            return hv_contract_compare_cpu_rule(golden, actual, rule, failure);
        case HV_CONTRACT_FIELD_IRQ_ROUTE:
            if (rule->kind != HV_CONTRACT_SET) {
                hv_contract_fail(failure, HV_CONTRACT_FIELD_SCHEMA, rule->index, rule->kind,
                                 HV_CONTRACT_SET, rule->kind);
                return false;
            }
            return hv_contract_compare_irq_set(golden, actual, failure);
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
