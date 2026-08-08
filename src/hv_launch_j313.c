/* SPDX-License-Identifier: MIT */

#include "hv_launch_j313.h"
#include "string.h"

#ifndef HV_LAUNCH_J313_HOST_TEST
#include "arm_cpu_regs.h"
#include "cpu_regs.h"
#include "utils.h"
#endif

static const struct hv_contract_rule j313_rules[] = {
    {.field = HV_CONTRACT_FIELD_IDENTITY_TARGET, .kind = HV_CONTRACT_EXACT},
    {.field = HV_CONTRACT_FIELD_BOOT_GUEST_ENTRY, .kind = HV_CONTRACT_EXACT},
    {.field = HV_CONTRACT_FIELD_BOOT_ARG, .index = 0, .kind = HV_CONTRACT_EXACT},
    {.field = HV_CONTRACT_FIELD_CPU_MPIDR, .index = HV_CONTRACT_ALL_ITEMS, .kind = HV_CONTRACT_SET},
    {.field = HV_CONTRACT_FIELD_CPU_HACR,
     .index = HV_CONTRACT_ALL_ITEMS,
     .kind = HV_CONTRACT_MASKED,
     .mask = HV_J313_HACR_REQUIRED_MASK},
    {.field = HV_CONTRACT_FIELD_CPU_MDCR,
     .index = HV_CONTRACT_ALL_ITEMS,
     .kind = HV_CONTRACT_MASKED,
     .mask = HV_J313_MDCR_REQUIRED_MASK},
    {.field = HV_CONTRACT_FIELD_CPU_MDSCR,
     .index = HV_CONTRACT_ALL_ITEMS,
     .kind = HV_CONTRACT_MASKED,
     .mask = HV_J313_MDSCR_REQUIRED_MASK},
    {.field = HV_CONTRACT_FIELD_CPU_AMX_CONFIG,
     .index = HV_CONTRACT_ALL_ITEMS,
     .kind = HV_CONTRACT_MASKED,
     .mask = HV_J313_AMX_REQUIRED_MASK},
    {.field = HV_CONTRACT_FIELD_CPU_APVMKEYLO,
     .index = HV_CONTRACT_ALL_ITEMS,
     .kind = HV_CONTRACT_EXACT},
    {.field = HV_CONTRACT_FIELD_CPU_APVMKEYHI,
     .index = HV_CONTRACT_ALL_ITEMS,
     .kind = HV_CONTRACT_EXACT},
    {.field = HV_CONTRACT_FIELD_CPU_APSTS,
     .index = HV_CONTRACT_ALL_ITEMS,
     .kind = HV_CONTRACT_EXACT},
    {.field = HV_CONTRACT_FIELD_CPU_ACTLR,
     .index = HV_CONTRACT_ALL_ITEMS,
     .kind = HV_CONTRACT_MASKED,
     .mask = HV_J313_ACTLR_REQUIRED_MASK},
    {.field = HV_CONTRACT_FIELD_IRQ_ROUTE, .index = HV_CONTRACT_ALL_ITEMS, .kind = HV_CONTRACT_SET},
    {.field = HV_CONTRACT_FIELD_REGION,
     .index = HV_CONTRACT_REGION_HEAP,
     .kind = HV_CONTRACT_RELATIVE_REGION,
     .reference = HV_CONTRACT_REGION_GUEST_RAM},
    {.field = HV_CONTRACT_FIELD_REGION,
     .index = HV_CONTRACT_REGION_FRAMEBUFFER,
     .kind = HV_CONTRACT_RELATIVE_REGION,
     .reference = HV_CONTRACT_REGION_GUEST_RAM},
    {.field = HV_CONTRACT_FIELD_REGION,
     .index = HV_CONTRACT_REGION_DART_TABLES,
     .kind = HV_CONTRACT_RELATIVE_REGION,
     .reference = HV_CONTRACT_REGION_GUEST_RAM},
};

const struct hv_contract_schema HV_J313_CONTRACT_SCHEMA = {
    .version = HV_CONTRACT_VERSION,
    .rules = j313_rules,
    .rule_count = sizeof(j313_rules) / sizeof(j313_rules[0]),
};

static struct hv_launch_j313_host_state observed;
static bool observed_valid;

bool hv_launch_j313_set_base_state(const struct hv_launch_j313_host_state *state)
{
    observed_valid = false;
    if (!state || state->identity.target != HV_J313_TARGET ||
        state->identity.schema_revision != HV_J313_SCHEMA_REVISION || !state->cpu_count ||
        state->cpu_count > HV_CONTRACT_MAX_CPUS || state->region_count > HV_CONTRACT_MAX_REGIONS ||
        state->mapping_count > HV_CONTRACT_MAX_MAPPINGS ||
        state->irq_route_count > HV_CONTRACT_MAX_IRQ_ROUTES)
        return false;

    observed = *state;
    observed_valid = true;
    return true;
}

bool hv_launch_j313_fill_cpus(struct hv_launch_j313_host_state *state, const uint64_t *mpidrs,
                              uint32_t cpu_count,
                              const struct hv_launch_j313_cpu_registers *registers)
{
    if (!state || !mpidrs || !registers || !cpu_count || cpu_count > HV_CONTRACT_MAX_CPUS)
        return false;

    state->cpu_count = cpu_count;
    for (uint32_t i = 0; i < cpu_count; i++) {
        state->cpus[i] = (struct hv_contract_cpu){
            .mpidr = mpidrs[i],
            .hacr = registers->hacr,
            .mdcr = registers->mdcr,
            .mdscr = registers->mdscr,
            .amx_config = registers->amx_config,
            .apvmkeylo = registers->apvmkeylo,
            .apvmkeyhi = registers->apvmkeyhi,
            .apsts = registers->apsts,
            .actlr = registers->actlr,
        };
    }
    return true;
}

bool hv_launch_j313_fill_irq_routes(struct hv_launch_j313_host_state *state,
                                    const struct hv_irq_route *routes, size_t route_count)
{
    if (!state || (route_count && !routes) || route_count > HV_CONTRACT_MAX_IRQ_ROUTES)
        return false;

    state->irq_route_count = (uint32_t)route_count;
    for (size_t i = 0; i < route_count; i++) {
        state->irq_routes[i] = (struct hv_contract_irq_route){
            .physical_irq = routes[i].hw_irq,
            .vintid = routes[i].vintid,
            .flags = routes[i].level ? HV_CONTRACT_IRQ_LEVEL : 0,
        };
    }
    return true;
}

#ifndef HV_LAUNCH_J313_HOST_TEST
static bool hv_launch_j313_platform_read_state(struct hv_launch_j313_host_state *state)
{
    struct hv_launch_j313_cpu_registers registers;
    struct hv_irq_route irq_routes[HV_CONTRACT_MAX_IRQ_ROUTES];
    uint64_t mpidrs[HV_CONTRACT_MAX_CPUS];
    uint32_t cpu_count;
    size_t irq_route_count;

    if (!observed_valid)
        return false;

    *state = observed;

    cpu_count = state->cpu_count;
    for (uint32_t i = 0; i < cpu_count; i++)
        mpidrs[i] = state->cpus[i].mpidr;

    irq_route_count = hv_irq_route_count();
    if (irq_route_count > HV_CONTRACT_MAX_IRQ_ROUTES)
        return false;
    for (size_t i = 0; i < irq_route_count; i++) {
        const struct hv_irq_route *route = hv_irq_route_at(i);

        if (!route)
            return false;
        irq_routes[i] = *route;
    }
    if (!hv_launch_j313_fill_irq_routes(state, irq_routes, irq_route_count))
        return false;

    registers = (struct hv_launch_j313_cpu_registers){
        .hacr = mrs(HACR_EL2),
        .mdcr = mrs(MDCR_EL2),
        .mdscr = mrs(MDSCR_EL1),
        .amx_config = mrs(SYS_IMP_APL_AMX_CTL_EL1),
        .apvmkeylo = mrs(SYS_IMP_APL_APVMKEYLO_EL2),
        .apvmkeyhi = mrs(SYS_IMP_APL_APVMKEYHI_EL2),
        .apsts = mrs(SYS_IMP_APL_APSTS_EL12),
        .actlr = cpu_features->actlr_el2 ? mrs(SYS_ACTLR_EL12) : mrs(SYS_IMP_APL_ACTLR_EL12),
    };
    return hv_launch_j313_fill_cpus(state, mpidrs, cpu_count, &registers);
}
#endif

static bool read_identity(void *context, struct hv_contract_identity *out)
{
    const struct hv_launch_j313_host_state *state = context;

    *out = state->identity;
    return true;
}

static bool read_boot(void *context, struct hv_contract_boot *out)
{
    const struct hv_launch_j313_host_state *state = context;

    *out = state->boot;
    return true;
}

static bool read_adt(void *context, uint64_t *size, uint8_t digest[HV_CONTRACT_DIGEST_SIZE])
{
    const struct hv_launch_j313_host_state *state = context;

    *size = state->adt_size;
    memcpy(digest, state->adt_digest, HV_CONTRACT_DIGEST_SIZE);
    return true;
}

#define DEFINE_ARRAY_READER(name, member, count_member, type)                                      \
    static bool name(void *context, struct type *out, uint32_t capacity, uint32_t *count)          \
    {                                                                                              \
        const struct hv_launch_j313_host_state *state = context;                                   \
        if (state->count_member > capacity)                                                        \
            return false;                                                                          \
        *count = state->count_member;                                                              \
        memcpy(out, state->member, (size_t)*count * sizeof(*out));                                 \
        return true;                                                                               \
    }

DEFINE_ARRAY_READER(read_regions, regions, region_count, hv_contract_region)
DEFINE_ARRAY_READER(read_mappings, mappings, mapping_count, hv_contract_mapping)
DEFINE_ARRAY_READER(read_cpus, cpus, cpu_count, hv_contract_cpu)
DEFINE_ARRAY_READER(read_irq_routes, irq_routes, irq_route_count, hv_contract_irq_route)

static bool read_devices(void *context, struct hv_contract_devices *out)
{
    const struct hv_launch_j313_host_state *state = context;

    *out = state->devices;
    return true;
}

void hv_launch_j313_provider_init(struct hv_launch_snapshot_provider *provider)
{
    *provider = (struct hv_launch_snapshot_provider){
        .context = &observed,
        .read_identity = read_identity,
        .read_boot = read_boot,
        .read_adt = read_adt,
        .read_regions = read_regions,
        .read_mappings = read_mappings,
        .read_cpus = read_cpus,
        .read_irq_routes = read_irq_routes,
        .read_devices = read_devices,
    };
}

bool hv_launch_j313_capture(enum hv_contract_checkpoint checkpoint, uint32_t sequence,
                            struct hv_contract_snapshot *out)
{
    struct hv_launch_snapshot_provider provider;

#ifndef HV_LAUNCH_J313_HOST_TEST
    struct hv_launch_j313_host_state live;

    if (!hv_launch_j313_platform_read_state(&live))
        return false;
    observed = live;
#else
    if (!observed_valid)
        return false;
#endif
    hv_launch_j313_provider_init(&provider);
    return hv_launch_snapshot_collect(checkpoint, sequence, &provider, out);
}

#ifdef HV_LAUNCH_J313_HOST_TEST
void hv_launch_j313_host_set_state(const struct hv_launch_j313_host_state *state)
{
    (void)hv_launch_j313_set_base_state(state);
}
#endif
