/* SPDX-License-Identifier: MIT */

#include "hv_launch_golden_j313.h"
#include "hv_apple_input.generated.h"

#define GOLDEN_J313_TARGET          0x3331334aU
#define GOLDEN_J313_SCHEMA_REVISION 1

static const uint64_t j313_mpidrs[HV_CONTRACT_MAX_CPUS] = {
    0x0, 0x1, 0x2, 0x3, 0x10100, 0x10101, 0x10102, 0x10103,
};

static const uint8_t j313_adt_digest[HV_CONTRACT_DIGEST_SIZE] = {
    0x09, 0x12, 0xe5, 0x89, 0x6c, 0xe6, 0x16, 0xba,
    0xa5, 0x20, 0x6e, 0xa4, 0xeb, 0xb2, 0xc3, 0x45,
    0xd1, 0x87, 0xff, 0x99, 0x88, 0x51, 0x7a, 0xe2,
    0x41, 0x8c, 0x1c, 0xee, 0x17, 0xf3, 0xd2, 0x7c,
};

static void fill_cpu(struct hv_contract_cpu *cpu, uint64_t mpidr, bool initialized)
{
    *cpu = (struct hv_contract_cpu){
        .mpidr = mpidr,
        .hacr = initialized ? 0x317000000014000ULL : 0,
        .mdcr = initialized ? 0xf00 : 0,
        .mdscr = initialized ? 0x8000 : 0,
        .amx_config = initialized ? 0x4000000000000100ULL : 0x100,
        .apvmkeylo = initialized ? 0x4e7672476f6e6147ULL : 0,
        .apvmkeyhi = initialized ? 0x697665596f755570ULL : 0,
        .apsts = initialized ? 1 : 0,
        .actlr = initialized ? 0x1c00 : 0xc00,
    };
}

static void fill_common(struct hv_contract_snapshot *snapshot, uint32_t checkpoint,
                        bool apple_input_declared)
{
    snapshot->identity = (struct hv_contract_identity){
        .target = GOLDEN_J313_TARGET,
        .schema_revision = GOLDEN_J313_SCHEMA_REVISION,
    };
    snapshot->boot = (struct hv_contract_boot){
        .ram_base = 0x850000000ULL,
        .ram_size = 0x18f708000ULL,
        .guest_entry = 0x8510b4000ULL,
        .args = {0x8533e8000ULL, 0, 0, 0},
    };
    snapshot->adt_size = 0x5941c;
    for (uint32_t i = 0; i < HV_CONTRACT_DIGEST_SIZE; i++)
        snapshot->adt_digest[i] = j313_adt_digest[i];
    snapshot->regions[0] = (struct hv_contract_region){
        .kind = HV_CONTRACT_REGION_GUEST_RAM, .base = 0x850000000ULL, .size = 0x18f708000ULL};
    snapshot->regions[1] = (struct hv_contract_region){
        .kind = HV_CONTRACT_REGION_HEAP, .base = 0x850000000ULL, .size = 0x1000000ULL};
    snapshot->regions[2] = (struct hv_contract_region){
        .kind = HV_CONTRACT_REGION_FIRMWARE, .base = 0x8510b4000ULL, .size = 0x1d88000ULL};
    snapshot->regions[3] = (struct hv_contract_region){
        .kind = HV_CONTRACT_REGION_ADT, .base = 0x851000000ULL, .size = 0x5c000ULL};
    snapshot->regions[4] = (struct hv_contract_region){
        .kind = HV_CONTRACT_REGION_BOOT_ARGS, .base = 0x8533e8000ULL, .size = 0x4000ULL};
    snapshot->regions[5] = (struct hv_contract_region){
        .kind = HV_CONTRACT_REGION_FRAMEBUFFER, .base = 0x85f000000ULL, .size = 0xfa0000ULL};
    snapshot->regions[6] = (struct hv_contract_region){
        .kind = HV_CONTRACT_REGION_LOW_MEMORY, .base = 0x8a0100000ULL, .size = 0x3ff00000ULL};
    snapshot->regions[7] = (struct hv_contract_region){
        .kind = HV_CONTRACT_REGION_DART_TABLES, .flags = 1, .base = 0x850000000ULL};
    snapshot->region_count = 8;

    snapshot->cpu_count = HV_CONTRACT_MAX_CPUS;
    for (uint32_t i = 0; i < HV_CONTRACT_MAX_CPUS; i++)
        fill_cpu(&snapshot->cpus[i], j313_mpidrs[i], checkpoint != HV_CONTRACT_PRE_HV_INIT);

    snapshot->irq_routes[0] = (struct hv_contract_irq_route){
        .physical_irq = 857, .vintid = 857, .flags = HV_CONTRACT_IRQ_LEVEL};
    snapshot->irq_route_count = 1;
    if (apple_input_declared && checkpoint != HV_CONTRACT_PRE_HV_INIT) {
        snapshot->irq_routes[1] = (struct hv_contract_irq_route){
            .physical_irq = HV_APPLE_INPUT_PHYSICAL_PARENT_IRQ,
            .vintid = HV_APPLE_INPUT_GUEST_VINTID,
            .flags = HV_CONTRACT_IRQ_LEVEL,
        };
        snapshot->irq_route_count = 2;
    }
    snapshot->devices = (struct hv_contract_devices){
        .pci_ecam_base = 0x690000000ULL,
        .xhci_base = 0x502280000ULL,
        .dart_base = 0x502f00000ULL,
        .vuart_base = 0x235200000ULL,
        .display_base = 0x85f000000ULL,
        .display_width = 2560,
        .display_height = 1600,
        .display_stride = 10240,
    };
}

bool hv_launch_golden_j313_init(
    struct hv_contract_snapshot out[HV_LAUNCH_PREFLIGHT_BLOCKING_CHECKPOINTS],
    bool apple_input_declared)
{
    if (!out)
        return false;
    for (uint32_t i = 0; i < HV_LAUNCH_PREFLIGHT_BLOCKING_CHECKPOINTS; i++)
        out[i] = (struct hv_contract_snapshot){0};
    for (uint32_t i = 0; i < HV_LAUNCH_PREFLIGHT_BLOCKING_CHECKPOINTS; i++) {
        out[i].header.magic = HV_CONTRACT_MAGIC;
        out[i].header.version = HV_CONTRACT_VERSION;
        out[i].header.checkpoint = i;
        out[i].header.sequence = i + 1;
        fill_common(&out[i], i, apple_input_declared);
        if (!hv_contract_finalize(&out[i]))
            return false;
    }
    return true;
}
