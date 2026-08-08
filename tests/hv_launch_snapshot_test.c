#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/hv_launch_snapshot.h"

enum fake_call {
    FAKE_IDENTITY = 1,
    FAKE_BOOT,
    FAKE_ADT,
    FAKE_REGIONS,
    FAKE_MAPPINGS,
    FAKE_CPUS,
    FAKE_IRQS,
    FAKE_DEVICES,
};

struct fake_context {
    unsigned int calls[8];
    size_t call_count;
    uint32_t cpu_count;
    bool duplicate_cpu;
    bool overflow_regions;
};

static void record_call(struct fake_context *fake, enum fake_call call)
{
    assert(fake->call_count < sizeof(fake->calls) / sizeof(fake->calls[0]));
    fake->calls[fake->call_count++] = call;
}

static bool fake_identity(void *context, struct hv_contract_identity *out)
{
    struct fake_context *fake = context;

    record_call(fake, FAKE_IDENTITY);
    out->target = 0x3331334a; /* J313 */
    out->schema_revision = 1;
    return true;
}

static bool fake_boot(void *context, struct hv_contract_boot *out)
{
    struct fake_context *fake = context;

    record_call(fake, FAKE_BOOT);
    out->ram_base = 0x800000000ULL;
    out->ram_size = 0x200000000ULL;
    out->guest_entry = 0x851000000ULL;
    out->args[0] = 0x840000000ULL;
    return true;
}

static bool fake_adt(void *context, uint64_t *size, uint8_t digest[HV_CONTRACT_DIGEST_SIZE])
{
    struct fake_context *fake = context;

    record_call(fake, FAKE_ADT);
    *size = 0x4000;
    memset(digest, 0x3a, HV_CONTRACT_DIGEST_SIZE);
    return true;
}

static bool fake_regions(void *context, struct hv_contract_region *out, uint32_t capacity,
                         uint32_t *count)
{
    struct fake_context *fake = context;

    record_call(fake, FAKE_REGIONS);
    if (fake->overflow_regions) {
        *count = HV_CONTRACT_MAX_REGIONS + 1;
        return true;
    }
    assert(capacity >= 2);
    *count = 2;
    out[0] = (struct hv_contract_region){
        .kind = HV_CONTRACT_REGION_GUEST_RAM,
        .base = 0x800000000ULL,
        .size = 0x200000000ULL,
    };
    out[1] = (struct hv_contract_region){
        .kind = HV_CONTRACT_REGION_HEAP,
        .base = 0x850000000ULL,
        .size = 0x1000000ULL,
    };
    return true;
}

static bool fake_mappings(void *context, struct hv_contract_mapping *out, uint32_t capacity,
                          uint32_t *count)
{
    struct fake_context *fake = context;

    record_call(fake, FAKE_MAPPINGS);
    assert(capacity >= 1);
    *count = 1;
    out[0] = (struct hv_contract_mapping){
        .ipa = 0x100000,
        .pa = 0x8a0100000ULL,
        .size = 0x3ff00000,
        .attributes = 0x705,
    };
    return true;
}

static bool fake_cpus(void *context, struct hv_contract_cpu *out, uint32_t capacity,
                      uint32_t *count)
{
    struct fake_context *fake = context;

    record_call(fake, FAKE_CPUS);
    *count = fake->cpu_count;
    for (uint32_t i = 0; i < fake->cpu_count && i < capacity; i++) {
        out[i].mpidr = fake->duplicate_cpu && i == 1 ? 0 : i;
        out[i].actlr = 1ULL << 12;
    }
    return true;
}

static bool fake_irqs(void *context, struct hv_contract_irq_route *out, uint32_t capacity,
                      uint32_t *count)
{
    struct fake_context *fake = context;

    record_call(fake, FAKE_IRQS);
    assert(capacity >= 1);
    *count = 1;
    out[0] = (struct hv_contract_irq_route){.physical_irq = 0x2c0, .vintid = 64};
    return true;
}

static bool fake_devices(void *context, struct hv_contract_devices *out)
{
    struct fake_context *fake = context;

    record_call(fake, FAKE_DEVICES);
    out->pci_ecam_base = 0x690000000ULL;
    out->nvme_bar_base = 0x400000000ULL;
    out->xhci_base = 0x502280000ULL;
    out->display_base = 0x85f000000ULL;
    return true;
}

static struct hv_launch_snapshot_provider fake_provider(struct fake_context *fake)
{
    return (struct hv_launch_snapshot_provider){
        .context = fake,
        .read_identity = fake_identity,
        .read_boot = fake_boot,
        .read_adt = fake_adt,
        .read_regions = fake_regions,
        .read_mappings = fake_mappings,
        .read_cpus = fake_cpus,
        .read_irq_routes = fake_irqs,
        .read_devices = fake_devices,
    };
}

static void test_deterministic_collection(void)
{
    static const unsigned int expected_calls[] = {
        FAKE_IDENTITY, FAKE_BOOT, FAKE_ADT,  FAKE_REGIONS,
        FAKE_MAPPINGS, FAKE_CPUS, FAKE_IRQS, FAKE_DEVICES,
    };
    struct fake_context first_fake = {.cpu_count = 8};
    struct fake_context second_fake = {.cpu_count = 8};
    struct hv_launch_snapshot_provider first_provider = fake_provider(&first_fake);
    struct hv_launch_snapshot_provider second_provider = fake_provider(&second_fake);
    struct hv_contract_snapshot first;
    struct hv_contract_snapshot second;

    memset(&first, 0xa5, sizeof(first));
    memset(&second, 0x5a, sizeof(second));
    assert(hv_launch_snapshot_collect(HV_CONTRACT_PRE_GUEST, 4, &first_provider, &first));
    assert(hv_launch_snapshot_collect(HV_CONTRACT_PRE_GUEST, 4, &second_provider, &second));
    assert(memcmp(&first, &second, sizeof(first)) == 0);
    assert(first.cpu_count == 8);
    assert(first_fake.call_count == 8);
    assert(memcmp(first_fake.calls, expected_calls, sizeof(expected_calls)) == 0);
}

static void test_rejects_invalid_provider_results(void)
{
    struct fake_context fake = {.cpu_count = 9};
    struct hv_launch_snapshot_provider provider = fake_provider(&fake);
    struct hv_contract_snapshot snapshot;

    assert(!hv_launch_snapshot_collect(HV_CONTRACT_PRE_GUEST, 4, &provider, &snapshot));

    fake = (struct fake_context){.cpu_count = 8, .duplicate_cpu = true};
    provider = fake_provider(&fake);
    assert(!hv_launch_snapshot_collect(HV_CONTRACT_PRE_GUEST, 4, &provider, &snapshot));

    fake = (struct fake_context){.cpu_count = 8, .overflow_regions = true};
    provider = fake_provider(&fake);
    assert(!hv_launch_snapshot_collect(HV_CONTRACT_PRE_GUEST, 4, &provider, &snapshot));

    fake = (struct fake_context){.cpu_count = 8};
    provider = fake_provider(&fake);
    provider.read_devices = NULL;
    assert(!hv_launch_snapshot_collect(HV_CONTRACT_PRE_GUEST, 4, &provider, &snapshot));
    assert(!hv_launch_snapshot_collect(HV_CONTRACT_CHECKPOINT_COUNT, 4, &provider, &snapshot));
}

int main(void)
{
    test_deterministic_collection();
    test_rejects_invalid_provider_results();
    puts("hv_launch_snapshot_test: ok");
    return 0;
}
