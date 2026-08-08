/* SPDX-License-Identifier: MIT */

#include "hv_launch_snapshot.h"
#include "string.h"

static bool hv_launch_provider_complete(const struct hv_launch_snapshot_provider *provider)
{
    return provider && provider->read_identity && provider->read_boot && provider->read_adt &&
           provider->read_regions && provider->read_mappings && provider->read_cpus &&
           provider->read_irq_routes && provider->read_devices;
}

static bool hv_launch_cpu_affinities_unique(const struct hv_contract_snapshot *snapshot)
{
    for (uint32_t first = 0; first < snapshot->cpu_count; first++) {
        for (uint32_t second = first + 1; second < snapshot->cpu_count; second++) {
            if (snapshot->cpus[first].mpidr == snapshot->cpus[second].mpidr)
                return false;
        }
    }

    return true;
}

bool hv_launch_snapshot_collect(enum hv_contract_checkpoint checkpoint, uint32_t sequence,
                                const struct hv_launch_snapshot_provider *provider,
                                struct hv_contract_snapshot *out)
{
    uint64_t adt_size;
    uint32_t region_count;
    uint32_t mapping_count;
    uint32_t cpu_count;
    uint32_t irq_route_count;

    if (!out || checkpoint >= HV_CONTRACT_CHECKPOINT_COUNT ||
        !hv_launch_provider_complete(provider))
        return false;

    memset(out, 0, sizeof(*out));
    out->header.magic = HV_CONTRACT_MAGIC;
    out->header.version = HV_CONTRACT_VERSION;
    out->header.checkpoint = checkpoint;
    out->header.sequence = sequence;

    if (!provider->read_identity(provider->context, &out->identity) ||
        !provider->read_boot(provider->context, &out->boot) ||
        !provider->read_adt(provider->context, &adt_size, out->adt_digest) ||
        !provider->read_regions(provider->context, out->regions, HV_CONTRACT_MAX_REGIONS,
                                &region_count) ||
        region_count > HV_CONTRACT_MAX_REGIONS ||
        !provider->read_mappings(provider->context, out->mappings, HV_CONTRACT_MAX_MAPPINGS,
                                 &mapping_count) ||
        mapping_count > HV_CONTRACT_MAX_MAPPINGS ||
        !provider->read_cpus(provider->context, out->cpus, HV_CONTRACT_MAX_CPUS, &cpu_count) ||
        cpu_count > HV_CONTRACT_MAX_CPUS ||
        !provider->read_irq_routes(provider->context, out->irq_routes, HV_CONTRACT_MAX_IRQ_ROUTES,
                                   &irq_route_count) ||
        irq_route_count > HV_CONTRACT_MAX_IRQ_ROUTES ||
        !provider->read_devices(provider->context, &out->devices))
        return false;

    out->adt_size = adt_size;
    out->region_count = region_count;
    out->mapping_count = mapping_count;
    out->cpu_count = cpu_count;
    out->irq_route_count = irq_route_count;

    if (!hv_launch_cpu_affinities_unique(out))
        return false;

    return hv_contract_finalize(out);
}
