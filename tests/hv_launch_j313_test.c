#include <assert.h>
#include <stdio.h>

#include "../src/hv_launch_j313.h"

static const struct hv_contract_rule *find_rule(uint16_t field, uint16_t index)
{
    for (size_t i = 0; i < HV_J313_CONTRACT_SCHEMA.rule_count; i++) {
        const struct hv_contract_rule *rule = &HV_J313_CONTRACT_SCHEMA.rules[i];

        if (rule->field == field && rule->index == index)
            return rule;
    }
    return NULL;
}

static void assert_rule(uint16_t field, uint16_t index, uint16_t kind)
{
    const struct hv_contract_rule *rule = find_rule(field, index);

    assert(rule);
    assert(rule->kind == kind);
}

static void test_schema_classifies_j313_invariants(void)
{
    const struct hv_contract_rule *rule;

    assert(HV_J313_CONTRACT_SCHEMA.version == HV_CONTRACT_VERSION);
    assert_rule(HV_CONTRACT_FIELD_IDENTITY_TARGET, 0, HV_CONTRACT_EXACT);
    assert_rule(HV_CONTRACT_FIELD_BOOT_GUEST_ENTRY, 0, HV_CONTRACT_EXACT);
    assert_rule(HV_CONTRACT_FIELD_BOOT_ARG, 0, HV_CONTRACT_EXACT);
    assert_rule(HV_CONTRACT_FIELD_CPU_MPIDR, HV_CONTRACT_ALL_ITEMS, HV_CONTRACT_SET);
    assert_rule(HV_CONTRACT_FIELD_IRQ_ROUTE, HV_CONTRACT_ALL_ITEMS, HV_CONTRACT_SET);
    assert_rule(HV_CONTRACT_FIELD_MAPPING, HV_CONTRACT_ALL_ITEMS, HV_CONTRACT_SET);

    rule = find_rule(HV_CONTRACT_FIELD_CPU_HACR, HV_CONTRACT_ALL_ITEMS);
    assert(rule && rule->kind == HV_CONTRACT_MASKED);
    assert(rule->mask == HV_J313_HACR_REQUIRED_MASK);
    rule = find_rule(HV_CONTRACT_FIELD_CPU_ACTLR, HV_CONTRACT_ALL_ITEMS);
    assert(rule && rule->kind == HV_CONTRACT_MASKED);
    assert(rule->mask == HV_J313_ACTLR_REQUIRED_MASK);

    assert_rule(HV_CONTRACT_FIELD_CPU_APVMKEYLO, HV_CONTRACT_ALL_ITEMS, HV_CONTRACT_EXACT);
    assert_rule(HV_CONTRACT_FIELD_CPU_APVMKEYHI, HV_CONTRACT_ALL_ITEMS, HV_CONTRACT_EXACT);
    assert_rule(HV_CONTRACT_FIELD_CPU_APSTS, HV_CONTRACT_ALL_ITEMS, HV_CONTRACT_EXACT);

    rule = find_rule(HV_CONTRACT_FIELD_REGION, HV_CONTRACT_REGION_HEAP);
    assert(rule && rule->kind == HV_CONTRACT_RELATIVE_REGION);
    assert(rule->reference == HV_CONTRACT_REGION_GUEST_RAM);
    rule = find_rule(HV_CONTRACT_FIELD_REGION, HV_CONTRACT_REGION_FRAMEBUFFER);
    assert(rule && rule->kind == HV_CONTRACT_RELATIVE_REGION);
    assert(rule->reference == HV_CONTRACT_REGION_GUEST_RAM);
    rule = find_rule(HV_CONTRACT_FIELD_REGION, HV_CONTRACT_REGION_DART_TABLES);
    assert(rule && rule->kind == HV_CONTRACT_RELATIVE_REGION);
    assert(rule->reference == HV_CONTRACT_REGION_GUEST_RAM);
}

static struct hv_launch_j313_host_state valid_state(void)
{
    struct hv_launch_j313_host_state state = {0};

    state.identity.target = HV_J313_TARGET;
    state.identity.schema_revision = HV_J313_SCHEMA_REVISION;
    state.boot.ram_base = 0x800000000ULL;
    state.boot.ram_size = 0x200000000ULL;
    state.boot.guest_entry = 0x8510b4000ULL;
    state.boot.args[0] = 0x854000000ULL;
    state.region_count = HV_CONTRACT_REGION_DART_TABLES + 1;
    state.regions[HV_CONTRACT_REGION_GUEST_RAM] = (struct hv_contract_region){
        .kind = HV_CONTRACT_REGION_GUEST_RAM,
        .base = 0x800000000ULL,
        .size = 0x200000000ULL,
    };
    state.regions[HV_CONTRACT_REGION_HEAP] = (struct hv_contract_region){
        .kind = HV_CONTRACT_REGION_HEAP,
        .base = 0x850000000ULL,
        .size = 0x1000000ULL,
    };
    state.regions[HV_CONTRACT_REGION_FRAMEBUFFER] = (struct hv_contract_region){
        .kind = HV_CONTRACT_REGION_FRAMEBUFFER,
        .base = 0x85f000000ULL,
        .size = 0x3e8000ULL,
    };
    state.regions[HV_CONTRACT_REGION_DART_TABLES] = (struct hv_contract_region){
        .kind = HV_CONTRACT_REGION_DART_TABLES,
        .base = 0x880000000ULL,
        .size = 0x40000ULL,
    };
    state.adt_size = 0x4000;
    state.cpu_count = 1;
    state.cpus[0].mpidr = 0;
    state.cpus[0].hacr = HV_J313_HACR_REQUIRED_MASK;
    state.cpus[0].mdcr = HV_J313_MDCR_REQUIRED_MASK;
    state.cpus[0].mdscr = HV_J313_MDSCR_REQUIRED_MASK;
    state.cpus[0].amx_config = HV_J313_AMX_REQUIRED_MASK;
    state.cpus[0].actlr = HV_J313_ACTLR_REQUIRED_MASK;
    return state;
}

static void test_host_provider_captures_injected_state(void)
{
    struct hv_launch_j313_host_state state = valid_state();
    struct hv_contract_snapshot snapshot;

    hv_launch_j313_host_set_state(&state);
    assert(hv_launch_j313_capture(HV_CONTRACT_PRE_GUEST, 4, &snapshot));
    assert(snapshot.identity.target == HV_J313_TARGET);
    assert(snapshot.boot.guest_entry == state.boot.guest_entry);
    assert(snapshot.cpu_count == 1);
    assert(snapshot.cpus[0].hacr == HV_J313_HACR_REQUIRED_MASK);
}

static void test_schema_reports_required_hacr_bit(void)
{
    struct hv_launch_j313_host_state state = valid_state();
    struct hv_contract_snapshot golden;
    struct hv_contract_snapshot actual;
    struct hv_contract_failure failure = {0};

    hv_launch_j313_host_set_state(&state);
    assert(hv_launch_j313_capture(HV_CONTRACT_PRE_GUEST, 4, &golden));
    if (!hv_contract_compare(&golden, &golden, &HV_J313_CONTRACT_SCHEMA, &failure)) {
        fprintf(stderr,
                "unexpected self-compare failure field=%u index=%u rule=%u expected=%#llx "
                "actual=%#llx\n",
                failure.field, failure.index, failure.rule, (unsigned long long)failure.expected,
                (unsigned long long)failure.actual);
        assert(false);
    }
    state.cpus[0].hacr ^= 1ULL << 56;
    hv_launch_j313_host_set_state(&state);
    assert(hv_launch_j313_capture(HV_CONTRACT_PRE_GUEST, 4, &actual));

    assert(!hv_contract_compare(&golden, &actual, &HV_J313_CONTRACT_SCHEMA, &failure));
    assert(failure.field == HV_CONTRACT_FIELD_CPU_HACR);
    assert(failure.index == 0);
    assert(failure.rule == HV_CONTRACT_MASKED);
}

static void test_cpu_image_is_applied_to_every_mpidr(void)
{
    const uint64_t mpidrs[] = {0, 1, 2, 3, 0x100, 0x101, 0x102, 0x103};
    const struct hv_launch_j313_cpu_registers registers = {
        .hacr = HV_J313_HACR_REQUIRED_MASK,
        .mdcr = HV_J313_MDCR_REQUIRED_MASK,
        .mdscr = HV_J313_MDSCR_REQUIRED_MASK,
        .amx_config = HV_J313_AMX_REQUIRED_MASK,
        .apvmkeylo = 0x4e7672476f6e6147ULL,
        .apvmkeyhi = 0x697665596f755570ULL,
        .apsts = 1,
        .actlr = HV_J313_ACTLR_REQUIRED_MASK,
    };
    struct hv_launch_j313_host_state state = {0};

    assert(hv_launch_j313_fill_cpus(&state, mpidrs, 8, &registers));
    assert(state.cpu_count == 8);
    for (uint32_t i = 0; i < state.cpu_count; i++) {
        assert(state.cpus[i].mpidr == mpidrs[i]);
        assert(state.cpus[i].hacr == registers.hacr);
        assert(state.cpus[i].actlr == registers.actlr);
    }
    assert(!hv_launch_j313_fill_cpus(&state, mpidrs, 9, &registers));
    assert(!hv_launch_j313_fill_cpus(&state, mpidrs, 8, NULL));
}

static void test_base_state_publication_is_fail_closed(void)
{
    struct hv_launch_j313_host_state state = valid_state();

    assert(!hv_launch_j313_set_base_state(NULL));
    state.identity.target = 0;
    assert(!hv_launch_j313_set_base_state(&state));
    state = valid_state();
    assert(hv_launch_j313_set_base_state(&state));
}

static void test_irq_routes_are_converted_from_live_route_records(void)
{
    const struct hv_irq_route routes[] = {
        {.hw_irq = 857, .vintid = 64, .level = true},
        {.hw_irq = 900, .vintid = 65, .level = false},
    };
    struct hv_launch_j313_host_state state = valid_state();

    assert(hv_launch_j313_fill_irq_routes(&state, routes, 2));
    assert(state.irq_route_count == 2);
    assert(state.irq_routes[0].physical_irq == 857);
    assert(state.irq_routes[0].vintid == 64);
    assert(state.irq_routes[0].flags == HV_CONTRACT_IRQ_LEVEL);
    assert(state.irq_routes[1].physical_irq == 900);
    assert(state.irq_routes[1].vintid == 65);
    assert(state.irq_routes[1].flags == 0);
    assert(!hv_launch_j313_fill_irq_routes(&state, routes, HV_CONTRACT_MAX_IRQ_ROUTES + 1));
    assert(!hv_launch_j313_fill_irq_routes(&state, NULL, 1));
}

static void test_descriptor_publishes_boot_regions_and_cpu_affinities(void)
{
    struct hv_launch_j313_descriptor descriptor = {0};
    struct hv_contract_snapshot snapshot;

    descriptor.identity.target = HV_J313_TARGET;
    descriptor.identity.schema_revision = HV_J313_SCHEMA_REVISION;
    descriptor.boot.ram_base = 0x800000000ULL;
    descriptor.boot.ram_size = 0x200000000ULL;
    descriptor.boot.guest_entry = 0x8510b4000ULL;
    descriptor.boot.args[0] = 0x854000000ULL;
    descriptor.cpu_count = 2;
    descriptor.mpidrs[0] = 0;
    descriptor.mpidrs[1] = 1;
    descriptor.region_count = 1;
    descriptor.regions[0] = (struct hv_contract_region){
        .kind = HV_CONTRACT_REGION_GUEST_RAM,
        .base = descriptor.boot.ram_base,
        .size = descriptor.boot.ram_size,
    };

    assert(hv_launch_j313_publish_descriptor(&descriptor));
    assert(hv_launch_j313_capture(HV_CONTRACT_PRE_GUEST, 4, &snapshot));
    assert(snapshot.boot.guest_entry == descriptor.boot.guest_entry);
    assert(snapshot.region_count == 1);
    assert(snapshot.cpu_count == 2);
    assert(snapshot.cpus[0].mpidr == 0);
    assert(snapshot.cpus[1].mpidr == 1);

    descriptor.cpu_count = HV_CONTRACT_MAX_CPUS + 1;
    assert(!hv_launch_j313_publish_descriptor(&descriptor));
}

int main(void)
{
    test_schema_classifies_j313_invariants();
    test_host_provider_captures_injected_state();
    test_schema_reports_required_hacr_bit();
    test_cpu_image_is_applied_to_every_mpidr();
    test_base_state_publication_is_fail_closed();
    test_irq_routes_are_converted_from_live_route_records();
    test_descriptor_publishes_boot_regions_and_cpu_affinities();
    puts("hv_launch_j313_test: ok");
    return 0;
}
