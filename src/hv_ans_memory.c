/* SPDX-License-Identifier: MIT */
#include "hv_ans_memory.h"

/* Current hv_vm.c: 16 KiB L3, 32 MiB L2; no LPA2/indirect permissions.
 * Arm stage-2 descriptor: AF[10], SH[9:8], S2AP[7:6], MemAttr[5:2].
 * hv_map_hw installs normal WB attributes 1111 and inner-shareable 11.
 * Reject other attributes rather than infer equivalent cache/DMA behavior.
 * Permissions: S2AP[0] read, S2AP[1] write (Arm DDI0487 stage-2 access
 * controls; Arm IHI0070 G.a 3.13.3 also explicitly identifies bit1 as write).
 * Official source: https://documentation-service.arm.com/static/66c5c097882fec713ef4a8ff
 * No Arm/SMMU implementation code is copied or used as ANS initialization. */
#define S2_OA_MASK UINT64_C(0x0003ffffffffc000)
#define S2_AF UINT64_C(0x400)
#define S2_SH UINT64_C(0x300)
#define S2_MEM UINT64_C(0x3c)
#define S2_AP UINT64_C(0xc0)
#define S2_ALLOWED (S2_OA_MASK | S2_AF | S2_SH | S2_MEM | S2_AP | UINT64_C(3))

static bool valid_range(uint64_t base, uint64_t size)
{
    return base <= UINT64_MAX - size;
}
static bool contains(uint64_t base, uint64_t size, uint64_t addr, uint64_t bytes)
{
    return bytes && bytes <= size && addr >= base && addr - base <= size - bytes;
}
static bool overlaps(uint64_t base, uint64_t size, uint64_t addr, uint64_t bytes)
{
    return size && bytes && base < addr + bytes && addr < base + size;
}
static bool policy_valid(const struct ans_memory_context *c)
{
    const struct hv_contract_snapshot *s = c->launch;
    unsigned seen = 0;
    if (!s || !c->reservations_complete || !c->raw_leaf ||
        c->ipa_bits < 32 || c->ipa_bits > 44 ||
        c->private_count > ANS_MEMORY_MAX_PRIVATE_RANGES ||
        (c->private_count && !c->private_pa) ||
        !valid_range(c->ramdisk.base, c->ramdisk.size) ||
        s->header.magic != HV_CONTRACT_MAGIC || s->header.version != HV_CONTRACT_VERSION ||
        !s->region_count || s->region_count > HV_CONTRACT_MAX_REGIONS ||
        !s->boot.ram_size || !valid_range(s->boot.ram_base, s->boot.ram_size))
        return false;
    for (size_t i = 0; i < c->private_count; ++i)
        if (!c->private_pa[i].size || !valid_range(c->private_pa[i].base, c->private_pa[i].size))
            return false;
    for (unsigned i = 0; i < s->region_count; ++i) {
        const struct hv_contract_region *r = &s->regions[i];
        if (r->kind > HV_CONTRACT_REGION_DART_TABLES || r->flags ||
            !valid_range(r->base, r->size) || (seen & (1u << r->kind)))
            return false;
        seen |= 1u << r->kind;
    }
    /* Current launch snapshots include all eight kinds. An omitted reservation
     * is not evidence of its absence. Explicit zero sizes are permitted. */
    return seen == ((1u << (HV_CONTRACT_REGION_DART_TABLES + 1)) - 1);
}
/* Launch-layout labels inside GUEST_RAM (load_raw pad, guest ADT/TrustCache copy, boot args,
 * firmware image) describe pre-guest placement only. Native ANS DMA starts after the EBS
 * ownership handoff, when the guest CPU already owns these pages through stage-2 and Windows
 * reuses them (ANS006-R6/R8 denials at 0x8509c9000.., 0x851310000). EL2-owned, iBoot carveout,
 * private, framebuffer, RAM-disk and DART-table memory remain excluded. */
static bool guest_owned_after_handoff(uint32_t kind)
{
    return kind == HV_CONTRACT_REGION_HEAP || kind == HV_CONTRACT_REGION_FIRMWARE ||
           kind == HV_CONTRACT_REGION_ADT || kind == HV_CONTRACT_REGION_BOOT_ARGS;
}
static bool backing_allowed(const struct ans_memory_context *c, uint64_t pa, uint64_t n)
{
    const struct hv_contract_snapshot *s = c->launch;
    bool owner = false;
    if (!valid_range(pa, n) || !contains(s->boot.ram_base, s->boot.ram_size, pa, n) ||
        overlaps(c->ramdisk.base, c->ramdisk.size, pa, n))
        return false;
    for (unsigned i = 0; i < s->region_count; ++i) {
        const struct hv_contract_region *r = &s->regions[i];
        if (r->kind == HV_CONTRACT_REGION_GUEST_RAM) {
            owner = contains(r->base, r->size, pa, n);
        } else if (r->kind != HV_CONTRACT_REGION_LOW_MEMORY &&
                   !(c->heap_sealed_below_guest && guest_owned_after_handoff(r->kind)) &&
                   overlaps(r->base, r->size, pa, n)) {
            return false;
        }
    }
    for (size_t i = 0; i < c->private_count; ++i)
        if (overlaps(c->private_pa[i].base, c->private_pa[i].size, pa, n))
            return false;
    /* LOW_MEMORY is an alias backing description, not an ownership grant and
     * not a reservation (hv_autonomous_runtime.publish_runtime_launch_state). */
    return owner;
}
bool ans_memory_translate(void *context, uint64_t ipa, size_t bytes,
                          uint32_t direction, uint64_t *out)
{
    const struct ans_memory_context *c = context;
    uint64_t first = 0, consumed = 0;
    if (!out) return false;
    *out = 0;
    if (!c || !bytes || bytes > ANS_MAX_TRANSFER || !valid_range(ipa, bytes) ||
        (direction != ANS_DMA_FROM_DEVICE && direction != ANS_DMA_TO_DEVICE) ||
        !policy_valid(c))
        return false;
    uint64_t limit = UINT64_C(1) << c->ipa_bits;
    if (ipa >= limit || bytes > limit - ipa) return false;
    while (consumed < bytes) {
        uint64_t raw = 0;
        uint8_t shift = 0;
        uint64_t cursor = ipa + consumed;
        if (!c->raw_leaf(c->leaf_context, cursor, &raw, &shift) ||
            (shift != 14 && shift != 25) ||
            (raw & 3) != (shift == 14 ? 3u : 1u) ||
            (raw & ~S2_ALLOWED) || (raw & (S2_AF | S2_SH | S2_MEM)) !=
                                    (S2_AF | S2_SH | S2_MEM))
            return false;
        uint64_t required = direction == ANS_DMA_FROM_DEVICE ? UINT64_C(0x80) : UINT64_C(0x40);
        if (!(raw & required)) return false;
        uint64_t span = UINT64_C(1) << shift;
        uint64_t offset = cursor & (span - 1);
        uint64_t base = raw & S2_OA_MASK;
        if (base & (span - 1)) return false;
        uint64_t n = span - offset;
        if (n > bytes - consumed) n = bytes - consumed;
        uint64_t pa = base + offset;
        if (!backing_allowed(c, pa, n)) return false;
        if (!consumed) first = pa;
        else if (!valid_range(first, consumed) || pa != first + consumed) return false;
        consumed += n;
    }
    if (!valid_range(first, bytes)) return false;
    *out = first;
    return true;
}

bool ans_memory_context_valid(const struct ans_memory_context *context)
{ return context && policy_valid(context); }

bool ans_memory_carveout_exclusions(const struct ans_memory_range *carveouts, size_t n,
                                    struct ans_memory_range guest,
                                    struct ans_memory_range kernel,
                                    struct ans_memory_range *out, size_t cap, size_t *count)
{
    if (!count || (n && !carveouts) || !out || *count > cap || !guest.size ||
        !valid_range(guest.base, guest.size) || !valid_range(kernel.base, kernel.size))
        return false;
    for (size_t i = 0; i < n; ++i) {
        struct ans_memory_range r = carveouts[i];
        if (!r.size && !r.base) continue;            /* unused ADT slot */
        if (!r.size || !valid_range(r.base, r.size)) return false;
        if (r.base == kernel.base && r.size == kernel.size) continue;
        if (!overlaps(guest.base, guest.size, r.base, r.size)) continue;
        if (*count >= cap) return false;
        out[(*count)++] = r;
    }
    return true;
}
