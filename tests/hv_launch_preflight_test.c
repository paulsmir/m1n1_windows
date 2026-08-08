#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/hv_launch_preflight.h"

static const struct hv_contract_rule rules[] = {
    {.field = HV_CONTRACT_FIELD_CPU_ACTLR,
     .index = HV_CONTRACT_ALL_ITEMS,
     .kind = HV_CONTRACT_MASKED,
     .mask = 1ULL << 12},
};
static const struct hv_contract_schema schema = {
    .version = HV_CONTRACT_VERSION,
    .rules = rules,
    .rule_count = 1,
};

static struct hv_contract_snapshot sample(unsigned int checkpoint, unsigned int sequence)
{
    struct hv_contract_snapshot value = {0};
    value.header.magic = HV_CONTRACT_MAGIC;
    value.header.version = HV_CONTRACT_VERSION;
    value.header.checkpoint = checkpoint;
    value.header.sequence = sequence;
    value.cpu_count = 2;
    value.cpus[0].mpidr = 0;
    value.cpus[1].mpidr = 1;
    value.cpus[0].actlr = value.cpus[1].actlr = 1ULL << 12;
    assert(hv_contract_finalize(&value));
    return value;
}

static bool entered(void *opaque)
{
    unsigned int *count = opaque;
    (*count)++;
    return true;
}

int main(void)
{
    struct hv_contract_snapshot golden[HV_LAUNCH_PREFLIGHT_BLOCKING_CHECKPOINTS];
    struct hv_launch_preflight preflight;
    unsigned int entries = 0;

    for (unsigned int i = 0; i < HV_LAUNCH_PREFLIGHT_BLOCKING_CHECKPOINTS; i++)
        golden[i] = sample(i, i + 1);

    assert(hv_launch_preflight_init(&preflight, golden,
                                    HV_LAUNCH_PREFLIGHT_BLOCKING_CHECKPOINTS, &schema));
    for (unsigned int i = 0; i < 3; i++)
        assert(hv_launch_preflight_check(&preflight, &golden[i]));

    struct hv_contract_snapshot bad = golden[3];
    bad.cpus[1].actlr = 0;
    assert(hv_contract_finalize(&bad));
    assert(!hv_launch_preflight_check(&preflight, &bad));
    assert(!hv_launch_preflight_enter(&preflight, entered, &entries));
    assert(entries == 0);
    assert(preflight.failure.field == HV_CONTRACT_FIELD_CPU_ACTLR);
    assert(preflight.failure.index == 1);

    static const struct hv_contract_rule early_rules[] = {
        {.field = HV_CONTRACT_FIELD_IDENTITY_TARGET, .kind = HV_CONTRACT_EXACT},
    };
    static const struct hv_contract_schema early_schema = {
        .version = HV_CONTRACT_VERSION,
        .rules = early_rules,
        .rule_count = sizeof(early_rules) / sizeof(early_rules[0]),
    };
    assert(hv_launch_preflight_init(&preflight, golden,
                                    HV_LAUNCH_PREFLIGHT_BLOCKING_CHECKPOINTS, &schema));
    struct hv_contract_snapshot early = golden[0];
    early.cpus[0].actlr = 0;
    assert(hv_contract_finalize(&early));
    assert(hv_launch_preflight_check_schema(&preflight, &early, &early_schema));
    assert(preflight.next == 1);
    assert(!preflight.blocked);

    assert(hv_launch_preflight_init(&preflight, golden,
                                    HV_LAUNCH_PREFLIGHT_BLOCKING_CHECKPOINTS, &schema));
    for (unsigned int i = 0; i < HV_LAUNCH_PREFLIGHT_BLOCKING_CHECKPOINTS; i++)
        assert(hv_launch_preflight_check(&preflight, &golden[i]));
    assert(hv_launch_preflight_enter(&preflight, entered, &entries));
    assert(entries == 1);

    puts("hv_launch_preflight_test: ok");
    return 0;
}
