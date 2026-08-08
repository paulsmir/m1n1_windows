#include <assert.h>
#include <stdio.h>

#include "../src/hv_launch_contract.h"

static const struct hv_contract_rule TEST_RULES[] = {
    {
        .field = HV_CONTRACT_FIELD_REGION,
        .index = HV_CONTRACT_REGION_HEAP,
        .kind = HV_CONTRACT_RELATIVE_REGION,
        .reference = HV_CONTRACT_REGION_GUEST_RAM,
    },
    {
        .field = HV_CONTRACT_FIELD_CPU_ACTLR,
        .index = 1,
        .kind = HV_CONTRACT_EXACT,
    },
};

static const struct hv_contract_schema J313_TEST_SCHEMA = {
    .version = HV_CONTRACT_VERSION,
    .rules = TEST_RULES,
    .rule_count = sizeof(TEST_RULES) / sizeof(TEST_RULES[0]),
};

static struct hv_contract_snapshot test_snapshot(void)
{
    struct hv_contract_snapshot snapshot = {
        .header =
            {
                .magic = HV_CONTRACT_MAGIC,
                .version = HV_CONTRACT_VERSION,
                .checkpoint = HV_CONTRACT_PRE_GUEST,
                .sequence = 4,
            },
        .region_count = 2,
        .cpu_count = 2,
        .regions =
            {
                [HV_CONTRACT_REGION_GUEST_RAM] =
                    {
                        .kind = HV_CONTRACT_REGION_GUEST_RAM,
                        .base = 0x800000000ULL,
                        .size = 0x200000000ULL,
                    },
                [HV_CONTRACT_REGION_HEAP] =
                    {
                        .kind = HV_CONTRACT_REGION_HEAP,
                        .base = 0x850000000ULL,
                        .size = 0x1000000ULL,
                    },
            },
        .cpus =
            {
                [0] = {.mpidr = 0, .actlr = 1ULL << 12},
                [1] = {.mpidr = 1, .actlr = 1ULL << 12},
            },
    };

    return snapshot;
}

static void test_relative_region_and_exact_actlr(void)
{
    struct hv_contract_failure failure = {0};
    struct hv_contract_snapshot golden = test_snapshot();
    struct hv_contract_snapshot actual = golden;

    actual.regions[HV_CONTRACT_REGION_GUEST_RAM].base += 0x200000;
    actual.regions[HV_CONTRACT_REGION_HEAP].base += 0x200000;
    assert(hv_contract_finalize(&golden));
    assert(hv_contract_finalize(&actual));
    assert(hv_contract_compare(&golden, &actual, &J313_TEST_SCHEMA, &failure));

    actual.cpus[1].actlr ^= 1ULL << 12;
    assert(hv_contract_finalize(&actual));
    assert(!hv_contract_compare(&golden, &actual, &J313_TEST_SCHEMA, &failure));
    assert(failure.field == HV_CONTRACT_FIELD_CPU_ACTLR);
    assert(failure.index == 1);
}

static void test_corrupt_payload_reports_checksum(void)
{
    struct hv_contract_failure failure = {0};
    struct hv_contract_snapshot golden = test_snapshot();
    struct hv_contract_snapshot actual = golden;

    assert(hv_contract_finalize(&golden));
    assert(hv_contract_finalize(&actual));
    actual.cpus[0].actlr ^= 1;
    assert(!hv_contract_compare(&golden, &actual, &J313_TEST_SCHEMA, &failure));
    assert(failure.field == HV_CONTRACT_FIELD_CHECKSUM);
}

static void test_invalid_cpu_rule_reports_schema_path(void)
{
    static const struct hv_contract_rule bad_rules[] = {
        {
            .field = HV_CONTRACT_FIELD_CPU_ACTLR,
            .index = HV_CONTRACT_MAX_CPUS,
            .kind = HV_CONTRACT_EXACT,
        },
    };
    const struct hv_contract_schema bad_schema = {
        .version = HV_CONTRACT_VERSION,
        .rules = bad_rules,
        .rule_count = 1,
    };
    struct hv_contract_failure failure = {0};
    struct hv_contract_snapshot golden = test_snapshot();
    struct hv_contract_snapshot actual = golden;

    assert(hv_contract_finalize(&golden));
    assert(hv_contract_finalize(&actual));
    assert(!hv_contract_compare(&golden, &actual, &bad_schema, &failure));
    assert(failure.field == HV_CONTRACT_FIELD_SCHEMA);
    assert(failure.index == HV_CONTRACT_MAX_CPUS);
}

static void test_digest_mismatch_reports_digest_byte(void)
{
    static const struct hv_contract_rule digest_rules[] = {
        {
            .field = HV_CONTRACT_FIELD_ADT_DIGEST,
            .kind = HV_CONTRACT_DIGEST,
        },
    };
    const struct hv_contract_schema digest_schema = {
        .version = HV_CONTRACT_VERSION,
        .rules = digest_rules,
        .rule_count = 1,
    };
    struct hv_contract_failure failure = {0};
    struct hv_contract_snapshot golden = test_snapshot();
    struct hv_contract_snapshot actual = golden;

    golden.adt_size = actual.adt_size = 0x4000;
    actual.adt_digest[7] = 0x5a;
    assert(hv_contract_finalize(&golden));
    assert(hv_contract_finalize(&actual));
    assert(!hv_contract_compare(&golden, &actual, &digest_schema, &failure));
    assert(failure.field == HV_CONTRACT_FIELD_ADT_DIGEST);
    assert(failure.index == 7);
}

static void test_mapping_set_is_order_independent_and_detects_target_change(void)
{
    static const struct hv_contract_rule rules[] = {
        {.field = HV_CONTRACT_FIELD_MAPPING,
         .index = HV_CONTRACT_ALL_ITEMS,
         .kind = HV_CONTRACT_SET},
    };
    const struct hv_contract_schema schema = {
        .version = HV_CONTRACT_VERSION,
        .rules = rules,
        .rule_count = 1,
    };
    struct hv_contract_snapshot golden = test_snapshot();
    struct hv_contract_snapshot actual = golden;
    struct hv_contract_failure failure = {0};

    golden.mapping_count = 2;
    golden.mappings[0] = (struct hv_contract_mapping){
        .ipa = 0x1000, .pa = 0x800001000, .size = 0x4000, .attributes = 1};
    golden.mappings[1] =
        (struct hv_contract_mapping){.ipa = 0x5000, .pa = 0, .size = 0x4000, .attributes = 3};
    actual.mapping_count = 2;
    actual.mappings[0] = golden.mappings[1];
    actual.mappings[1] = golden.mappings[0];
    assert(hv_contract_finalize(&golden));
    assert(hv_contract_finalize(&actual));
    assert(hv_contract_compare(&golden, &actual, &schema, &failure));

    actual.mappings[1].pa += 0x4000;
    assert(hv_contract_finalize(&actual));
    assert(!hv_contract_compare(&golden, &actual, &schema, &failure));
    assert(failure.field == HV_CONTRACT_FIELD_MAPPING);
}

int main(void)
{
    test_relative_region_and_exact_actlr();
    test_corrupt_payload_reports_checksum();
    test_invalid_cpu_rule_reports_schema_path();
    test_digest_mismatch_reports_digest_byte();
    test_mapping_set_is_order_independent_and_detects_target_change();

    puts("hv_launch_contract_test: ok");
    return 0;
}
