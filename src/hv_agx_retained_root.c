#include "hv_agx_retained_root.h"
#include "../../drivers/apple-agx/shared/include/apple_agx_firmware_prefix.h"

#define PAGE_BYTES 0x4000ULL
#define PA_LIMIT (1ULL << 40)

static unsigned char valid_pa(unsigned long long pa)
{
    return pa && !(pa & (PAGE_BYTES - 1)) && pa < PA_LIMIT;
}

static unsigned char reserved_pa(const struct hv_agx_retained_root *c,
                                 unsigned long long pa)
{
    return pa >= c->RetainedPa && pa - c->RetainedPa < c->RegionBytes;
}

static unsigned char owned_pa(const struct hv_agx_retained_root *c,
                              unsigned long long pa)
{
    unsigned int i;
    if (c->SystemPage.Entries && c->SystemPage.PhysicalAddress == pa)
        return 1;
    for (i = 0; i < c->Inventory.PageCount; ++i)
        if (c->Pages[i].PhysicalAddress == pa)
            return 1;
    return 0;
}

/* This callback never receives firmware-private descendants: those pages are
 * deliberately absent from the inventory. Protect the borrowed root as well. */
static void release_page(void *context, const APPLE_AGX_UAT_PAGE *page)
{
    struct hv_agx_retained_root *c = context;
    if (reserved_pa(c, page->PhysicalAddress) || page->Entries == c->RetainedEntries)
        return;
    c->Ops.Sync(c->Ops.Context);
    c->Ops.ReleasePage(c->Ops.Context, page);
}

static unsigned char allocate_page(void *context, APPLE_AGX_UAT_PAGE *page)
{
    struct hv_agx_retained_root *c = context;
    unsigned int i, level = page->Level;
    *page = (APPLE_AGX_UAT_PAGE){0};
    page->Level = level;
    if (!c->Ops.AllocatePage(c->Ops.Context, page))
        return 0;
    /* A broken allocator must never cause a release of an already owned or
     * borrowed page. Fail closed before looking through its CPU pointer. */
    if (reserved_pa(c, page->PhysicalAddress) ||
        page->Entries == c->RetainedEntries || owned_pa(c, page->PhysicalAddress))
        return 0;
    for (i = 0; i < c->Inventory.PageCount; ++i)
        if (page->Entries && page->Entries == c->Pages[i].Entries)
            return 0;
    if (page->Entries && page->Entries == c->SystemPage.Entries)
        return 0;
    if (!valid_pa(page->PhysicalAddress) || !page->Entries ||
        ((unsigned long long)page->Entries & (sizeof(*page->Entries) - 1))) {
        release_page(c, page);
        return 0;
    }
    for (i = 0; i < 2048; ++i) {
        if (page->Entries[i]) {
            release_page(c, page);
            return 0;
        }
    }
    page->Level = level;
    return 1;
}

unsigned char hv_agx_retained_prefix_unchanged(struct hv_agx_retained_root *c)
{
    if (!c || !c->PrefixSaved)
        return 0;
    if (c->Roots.Ttbr1PhysicalAddress != c->RetainedPa ||
        c->RetainedEntries[0] != c->PrivatePrefix[0] ||
        c->RetainedEntries[1] != c->PrivatePrefix[1]) {
        c->PrefixUnchanged = 0;
        c->Tainted = 1;
    }
    return c->PrefixUnchanged;
}

static APPLE_AGX_UAT_PAGE *owned_child(struct hv_agx_retained_root *,
                                     unsigned long long, unsigned int);
static int owned_leaf(struct hv_agx_retained_root *, unsigned long long,
                      unsigned long long **);

static int arena_descriptor(unsigned int arena_class,
                            AGX_RR_ARENA_DESCRIPTOR *descriptor)
{
    if (!descriptor)
        return HV_AGX_RETAINED_INVALID;
    *descriptor = (AGX_RR_ARENA_DESCRIPTOR){0};
    descriptor->Version = AGX_RR_ARENA_VERSION;
    descriptor->Class = arena_class;
    if (arena_class == AGX_RR_ARENA_SHARED) {
        descriptor->Va = AGX_RR_SHARED_ARENA_VA;
        descriptor->Bytes = AGX_RR_SHARED_ARENA_BYTES;
    } else if (arena_class == AGX_RR_ARENA_TIMESTAMP) {
        descriptor->Va = AGX_RR_TIMESTAMP_ARENA_VA;
        descriptor->Bytes = AGX_RR_TIMESTAMP_ARENA_BYTES;
    } else {
        *descriptor = (AGX_RR_ARENA_DESCRIPTOR){0};
        return HV_AGX_RETAINED_RANGE;
    }
    if (!descriptor->Bytes || (descriptor->Va | descriptor->Bytes) & (PAGE_BYTES - 1) ||
        descriptor->Va > ~0ULL - descriptor->Bytes ||
        ((descriptor->Va >> J313_AGX_G2_UAT_LEVEL0_SHIFT) & 7ULL) != 2ULL ||
        (((descriptor->Va + descriptor->Bytes - 1) >>
          J313_AGX_G2_UAT_LEVEL0_SHIFT) & 7ULL) != 2ULL)
        return HV_AGX_RETAINED_RANGE;
    return HV_AGX_RETAINED_OK;
}

static unsigned char arena_contains(const struct hv_agx_retained_root *c,
                                    unsigned long long va)
{
    AGX_RR_ARENA_DESCRIPTOR descriptor;
    unsigned int arena_class;
    for (arena_class = AGX_RR_ARENA_SHARED;
         arena_class <= AGX_RR_ARENA_TIMESTAMP; ++arena_class) {
        if (!(c->ArenaQueriedMask & HV_AGX_RETAINED_ARENA_MASK(arena_class)) ||
            arena_descriptor(arena_class, &descriptor) != HV_AGX_RETAINED_OK)
            continue;
        if (va >= descriptor.Va && va - descriptor.Va < descriptor.Bytes)
            return 1;
    }
    return 0;
}

/* TTBR0 belongs to the broker, and can contain only the exact low alias's
 * slot 0. Retained TTBR1 owns only slot 2; never walk its private slots. */
static int check_tables(struct hv_agx_retained_root *c)
{
    unsigned int i, half;
    APPLE_AGX_UAT_PAGE *root0;
    unsigned long long slots[2];
    unsigned char referenced[HV_AGX_RETAINED_MAX_PAGES] = {0};
    if (c->Tainted || (c->PrefixSaved && !hv_agx_retained_prefix_unchanged(c)))
        return HV_AGX_RETAINED_TAINTED;
    if (c->Roots.Ttbr1PhysicalAddress != c->RetainedPa)
        goto tainted;
    root0 = owned_child(c, c->Roots.Ttbr0PhysicalAddress | 3ULL, 0);
    if (!root0)
        goto tainted;
    for (i = 1; i < 2048; ++i)
        if (root0->Entries[i])
            goto tainted;
    for (i = 3; i < 2048; ++i)
        if (c->RetainedEntries[i])
            goto tainted;
    slots[0] = root0->Entries[0];
    slots[1] = c->RetainedEntries[2];
    for (half = 0; half < 2; ++half) {
        APPLE_AGX_UAT_PAGE *level1;
        if (!slots[half])
            continue;
        level1 = owned_child(c, slots[half], 1);
        if (!level1 || referenced[level1 - c->Pages])
            goto tainted;
        referenced[level1 - c->Pages] = 1;
        for (i = 0; i < 2048; ++i) {
            APPLE_AGX_UAT_PAGE *level2;
            if (!level1->Entries[i])
                continue;
            level2 = owned_child(c, level1->Entries[i], 2);
            /* Shared pruning clears one parent before freeing a child. Table
             * aliases violate that ownership contract; data aliases do not. */
            if (!level2 || referenced[level2 - c->Pages])
                goto tainted;
            referenced[level2 - c->Pages] = 1;
        }
    }
    return HV_AGX_RETAINED_OK;
tainted:
    c->Tainted = 1;
    return HV_AGX_RETAINED_TAINTED;
}

static int active_epoch(struct hv_agx_retained_root *c, unsigned long long epoch)
{
    if (!c)
        return HV_AGX_RETAINED_INVALID;
    if (!epoch || c->Epoch != epoch || !c->Prepared || !c->Active)
        return HV_AGX_RETAINED_STATE;
    return check_tables(c);
}

static int map_result(APPLE_AGX_UAT_RESULT result)
{
    if (result == AppleAgxUatResultOk)
        return HV_AGX_RETAINED_OK;
    if (result == AppleAgxUatResultCapacity || result == AppleAgxUatResultAllocationFailed)
        return HV_AGX_RETAINED_ALLOCATION;
    return HV_AGX_RETAINED_OWNERSHIP;
}

int hv_agx_retained_prepare(struct hv_agx_retained_root *core,
                           unsigned long long retained_pa,
                           unsigned long long *retained_entries,
                           unsigned long long region_bytes,
                           unsigned long long epoch,
                           const struct hv_agx_retained_ops *ops)
{
    struct hv_agx_retained_ops saved_ops;
    unsigned long long previous_epoch;
    APPLE_AGX_UAT_PAGE root = {0};
    if (!core || !ops || !ops->AllocatePage || !ops->ReleasePage ||
        !ops->TranslateGuest || !ops->Sync || !retained_entries ||
        ((unsigned long long)retained_entries & (sizeof(*retained_entries) - 1)) ||
        !epoch || !AgxFwPrefixGeometry(retained_pa, region_bytes))
        return HV_AGX_RETAINED_INVALID;
    if (core->Prepared || core->Active || core->Tainted || epoch <= core->Epoch)
        return HV_AGX_RETAINED_STATE;
    previous_epoch = core->Epoch;
    saved_ops = *ops;
    *core = (struct hv_agx_retained_root){0};
    core->Epoch = previous_epoch;
    core->RetainedPa = retained_pa;
    core->RetainedEntries = retained_entries;
    core->RegionBytes = region_bytes;
    core->Ops = saved_ops;
    core->Allocator = (APPLE_AGX_UAT_ALLOCATOR){core, allocate_page, release_page};
    core->Inventory = (APPLE_AGX_UAT_INVENTORY){
        core->Pages, HV_AGX_RETAINED_MAX_PAGES, 0,
        core->UatMappings, HV_AGX_RETAINED_MAX_MAPPINGS + 1 + HV_AGX_RETAINED_IO_RANGES, 0};
    if (!allocate_page(core, &root))
        return HV_AGX_RETAINED_ALLOCATION;
    core->Pages[0] = root;
    core->Pages[1] = (APPLE_AGX_UAT_PAGE){retained_pa, retained_entries, 0};
    core->Inventory.PageCount = 2;
    core->Roots = (APPLE_AGX_UAT_ROOTS){root.PhysicalAddress, retained_pa};
    core->Epoch = epoch;
    core->NextHandle = 1;
    core->Prepared = 1;
    return HV_AGX_RETAINED_OK;
}

int hv_agx_retained_activate(struct hv_agx_retained_root *c)
{
    AGX_FW_PREFIX prefix = {0};
    APPLE_AGX_UAT_PAGE system = {0};
    APPLE_AGX_UAT_RESULT result;
    unsigned int i;
    if (!c)
        return HV_AGX_RETAINED_INVALID;
    if (!c->Prepared || c->Active)
        return HV_AGX_RETAINED_STATE;
    if (c->Tainted || (c->PrefixSaved && !hv_agx_retained_prefix_unchanged(c)))
        return HV_AGX_RETAINED_TAINTED;
    if (c->Roots.Ttbr1PhysicalAddress != c->RetainedPa)
        return HV_AGX_RETAINED_OWNERSHIP;
    for (i = 2; i < 2048; ++i)
        if (c->RetainedEntries[i])
            return HV_AGX_RETAINED_OWNERSHIP;
    prefix.Magic = AGX_FW_PREFIX_MAGIC;
    prefix.Version = 1;
    prefix.Size = sizeof(prefix);
    prefix.PrefixBytes = 16;
    prefix.Base = c->RetainedPa;
    prefix.Length = c->RegionBytes;
    prefix.Epoch = c->Epoch;
    prefix.Ready = 1;
    prefix.Entries[0] = c->RetainedEntries[0];
    prefix.Entries[1] = c->RetainedEntries[1];
    if (!AgxFwPrefixValid(&prefix, sizeof(prefix), c->Epoch))
        return HV_AGX_RETAINED_OWNERSHIP;
    c->PrivatePrefix[0] = prefix.Entries[0];
    c->PrivatePrefix[1] = prefix.Entries[1];
    c->PrefixSaved = c->PrefixUnchanged = 1;
    system.Level = 3; /* Data, never a page-table inventory entry. */
    if (!allocate_page(c, &system))
        return HV_AGX_RETAINED_ALLOCATION;
    c->SystemPage = system;
    result = AppleAgxUatMap(0, &c->Roots, HV_AGX_RETAINED_SYSTEM_VA,
        system.PhysicalAddress, PAGE_BYTES, AppleAgxUatFirmwarePrivateReadWrite,
        &c->Allocator, &c->Inventory);
    c->Ops.Sync(c->Ops.Context);
    if (check_tables(c) != HV_AGX_RETAINED_OK)
        return HV_AGX_RETAINED_TAINTED;
    if (result != AppleAgxUatResultOk) {
        release_page(c, &c->SystemPage);
        c->SystemPage = (APPLE_AGX_UAT_PAGE){0};
        return map_result(result);
    }
    c->SystemVa = HV_AGX_RETAINED_SYSTEM_VA;
    c->SystemBytes = PAGE_BYTES;
    c->Active = 1;
    return HV_AGX_RETAINED_OK;
}

int hv_agx_retained_map(struct hv_agx_retained_root *c, unsigned long long epoch,
                       unsigned long long va, unsigned long long ipa,
                       unsigned long long length, unsigned long long *handle_out)
{
    unsigned long long pa;
    unsigned int i;
    APPLE_AGX_UAT_RESULT mapped;
    int result;
    if (!handle_out)
        return HV_AGX_RETAINED_INVALID;
    *handle_out = 0;
    result = active_epoch(c, epoch);
    if (result)
        return result;
    if (length != PAGE_BYTES || (va | ipa) & (PAGE_BYTES - 1) ||
        !ipa || ipa > ~0ULL - (PAGE_BYTES - 1) ||
        (va != J313_AGX_G2_REGIONB_BUFFER_MGR_GPU_VA &&
         (va < HV_AGX_RETAINED_WINDOWS_VA ||
          (va >= HV_AGX_RETAINED_WINDOWS_END && !arena_contains(c, va)))))
        return HV_AGX_RETAINED_RANGE;
    for (i = 0; i < c->MappingCount; ++i)
        if (c->Mappings[i].Va == va)
            return HV_AGX_RETAINED_OWNERSHIP;
    if (c->MappingCount == HV_AGX_RETAINED_MAX_MAPPINGS || !c->NextHandle)
        return HV_AGX_RETAINED_ALLOCATION;
    pa = c->Ops.TranslateGuest(c->Ops.Context, ipa);
    if (!valid_pa(pa) || reserved_pa(c, pa) || owned_pa(c, pa))
        return HV_AGX_RETAINED_RANGE;
    mapped = AppleAgxUatMap(
        0, &c->Roots, va, pa, length,
        va == J313_AGX_G2_REGIONB_BUFFER_MGR_GPU_VA
            ? AppleAgxUatFirmwareGpuSharedReadWrite
            : AppleAgxUatFirmwareSharedReadWrite,
        &c->Allocator, &c->Inventory);
    c->Ops.Sync(c->Ops.Context);
    if (check_tables(c))
        return HV_AGX_RETAINED_TAINTED;
    if (mapped != AppleAgxUatResultOk)
        return map_result(mapped);
    c->Mappings[c->MappingCount++] = (struct hv_agx_retained_mapping){
        c->NextHandle, va, ipa, pa, length};
    *handle_out = c->NextHandle++;
    return HV_AGX_RETAINED_OK;
}

static struct hv_agx_retained_mapping *find_mapping(struct hv_agx_retained_root *c,
    unsigned long long handle, unsigned long long va, unsigned long long ipa,
    unsigned long long length)
{
    unsigned int i;
    for (i = 0; i < c->MappingCount; ++i) {
        struct hv_agx_retained_mapping *m = &c->Mappings[i];
        if (handle && m->Handle == handle && m->Va == va &&
            m->Ipa == ipa && m->Length == length)
            return m;
    }
    return 0;
}

/* The shared resolver returns a PA from the software image; it does not
 * establish hardware validity of intermediate descriptors. Require the exact
 * descriptor we publish and an owned page at the expected level first. */
static APPLE_AGX_UAT_PAGE *owned_child(struct hv_agx_retained_root *c,
                                     unsigned long long descriptor,
                                     unsigned int level)
{
    unsigned int i;
    for (i = 0; i < c->Inventory.PageCount; ++i) {
        APPLE_AGX_UAT_PAGE *page = &c->Pages[i];
        unsigned long long expected;
        if (page->Level == level && page->Entries &&
            page->Entries != c->RetainedEntries && valid_pa(page->PhysicalAddress) &&
            !reserved_pa(c, page->PhysicalAddress) &&
            AppleAgxUatEncodeTableDescriptor(page->PhysicalAddress, &expected) ==
                AppleAgxUatResultOk && descriptor == expected)
            return page;
    }
    return 0;
}

/* A NULL leaf proves a zero intermediate descriptor. Nonzero descriptors must
 * match an owned page before any dereference; resolver errors are not absence. */
static int owned_leaf(struct hv_agx_retained_root *c, unsigned long long va,
                      unsigned long long **leaf)
{
    unsigned long long descriptor;
    APPLE_AGX_UAT_PAGE *level1, *level2, *root0;
    *leaf = 0;
    if (va == J313_AGX_G2_REGIONB_BUFFER_MGR_GPU_VA) {
        root0 = owned_child(c, c->Roots.Ttbr0PhysicalAddress | 3ULL, 0);
        if (!root0)
            goto tainted;
        descriptor = root0->Entries[0];
    } else {
        if (va < HV_AGX_RETAINED_WINDOWS_VA ||
            ((va >> J313_AGX_G2_UAT_LEVEL0_SHIFT) & 7ULL) != 2ULL)
            goto tainted;
        descriptor = c->RetainedEntries[2];
    }
    if (!descriptor)
        return HV_AGX_RETAINED_OK;
    level1 = owned_child(c, descriptor, 1);
    if (!level1)
        goto tainted;
    descriptor = level1->Entries[(va >> J313_AGX_G2_UAT_LEVEL1_SHIFT) & 2047ULL];
    if (!descriptor)
        return HV_AGX_RETAINED_OK;
    level2 = owned_child(c, descriptor, 2);
    if (!level2)
        goto tainted;
    *leaf = &level2->Entries[(va >> J313_AGX_G2_UAT_LEVEL2_SHIFT) & 2047ULL];
    return HV_AGX_RETAINED_OK;
tainted:
    c->Tainted = 1;
    return HV_AGX_RETAINED_TAINTED;
}

int hv_agx_retained_query_arena(struct hv_agx_retained_root *c,
                                unsigned long long epoch,
                                unsigned int arena_class,
                                AGX_RR_ARENA_DESCRIPTOR *descriptor)
{
    AGX_RR_ARENA_DESCRIPTOR candidate;
    unsigned long long offset;
    int result;
    if (!descriptor)
        return HV_AGX_RETAINED_INVALID;
    *descriptor = (AGX_RR_ARENA_DESCRIPTOR){0};
    result = active_epoch(c, epoch);
    if (result)
        return result;
    result = arena_descriptor(arena_class, &candidate);
    if (result)
        return result;
    /* ACTIVATE already rejects every foreign slot-2 root descriptor.  Walk the
     * broker-owned descendants read-only as well so a repeated or partially
     * occupied arena can never be advertised as free. */
    for (offset = 0; offset < candidate.Bytes; offset += PAGE_BYTES) {
        unsigned long long *leaf = 0;
        result = owned_leaf(c, candidate.Va + offset, &leaf);
        if (result)
            return result;
        if (leaf && *leaf)
            return HV_AGX_RETAINED_OWNERSHIP;
    }
    c->ArenaQueriedMask |= HV_AGX_RETAINED_ARENA_MASK(arena_class);
    *descriptor = candidate;
    return HV_AGX_RETAINED_OK;
}

static int check_leaf(struct hv_agx_retained_root *c, unsigned long long va,
                      unsigned long long expected_pa,
                      APPLE_AGX_UAT_PROTECTION protection)
{
    unsigned long long pa, descriptor, expected, *leaf;
    if (owned_leaf(c, va, &leaf) || !leaf)
        goto tainted;
    if (AppleAgxUatResolvePage(0, &c->Roots, va, &c->Inventory, &pa, &descriptor) !=
        AppleAgxUatResultOk || pa != expected_pa ||
        AppleAgxUatEncodePageDescriptor(0, expected_pa, protection, &expected) !=
        AppleAgxUatResultOk || descriptor != expected)
        goto tainted;
    return HV_AGX_RETAINED_OK;
tainted:
    c->Tainted = 1;
    return HV_AGX_RETAINED_TAINTED;
}

static unsigned long long io_length(const AGX_FW_IO_DESCRIPTOR *d)
{
    return (d->Size + (d->Phys & (PAGE_BYTES - 1)) + PAGE_BYTES - 1) & ~(PAGE_BYTES - 1);
}

/* Check actual owned descriptors, including the guard after each IO range.
 * Partial preparation validates only the obligations recorded as mapped. */
static int check_io(struct hv_agx_retained_root *c, unsigned char complete)
{
    unsigned int slot, expected_slots = 0;
    for (slot = 0; slot < AGX_FW_IO_SLOTS; ++slot) {
        AGX_FW_IO_DESCRIPTOR d;
        unsigned long long offset, length, va, *leaf;
        if (!AgxFwIoProfile(slot, &d))
            goto tainted;
        if (!d.Size)
            continue;
        expected_slots |= 1u << slot;
        if (!(c->IoMappedSlots & (1u << slot)))
            continue;
        length = io_length(&d);
        va = d.Virt & ~(PAGE_BYTES - 1);
        for (offset = 0; offset < length; offset += PAGE_BYTES)
            if (check_leaf(c, va + offset, (d.Phys & ~(PAGE_BYTES - 1)) + offset,
                           AppleAgxUatFirmwareDeviceReadWrite))
                goto tainted;
        if (owned_leaf(c, va + length, &leaf) || (leaf && *leaf))
            goto tainted;
    }
    if ((c->IoMappedSlots & ~expected_slots) ||
        (complete && c->IoMappedSlots != expected_slots))
        goto tainted;
    return HV_AGX_RETAINED_OK;
tainted:
    c->Tainted = 1;
    return HV_AGX_RETAINED_TAINTED;
}

int hv_agx_retained_io_prepare(struct hv_agx_retained_root *c, unsigned long long epoch)
{
    unsigned int slot;
    int result = active_epoch(c, epoch);
    if (result)
        return result;
    if (c->IoAttempted)
        return c->IoReady ? check_io(c, 1) : HV_AGX_RETAINED_STATE;
    c->IoAttempted = 1;
    for (slot = 0; slot < AGX_FW_IO_SLOTS; ++slot) {
        AGX_FW_IO_DESCRIPTOR d;
        APPLE_AGX_UAT_RESULT mapped;
        if (!AgxFwIoProfile(slot, &d))
            return HV_AGX_RETAINED_INVALID;
        if (!d.Size)
            continue;
        mapped = AppleAgxUatMap(0, &c->Roots, d.Virt & ~(PAGE_BYTES - 1),
            d.Phys & ~(PAGE_BYTES - 1), io_length(&d), AppleAgxUatFirmwareDeviceReadWrite,
            &c->Allocator, &c->Inventory);
        if (mapped == AppleAgxUatResultOk)
            c->IoMappedSlots |= 1u << slot;
        c->Ops.Sync(c->Ops.Context);
        if (check_tables(c))
            return HV_AGX_RETAINED_TAINTED;
        /* A failed attempt is never published or retried while active. Shared
         * map unwinds only its own incomplete range. Earlier successful IO
         * ranges remain recorded until stopped CLOSE; MMIO backing is borrowed. */
        if (mapped != AppleAgxUatResultOk)
            return map_result(mapped);
    }
    result = check_io(c, 1);
    if (!result)
        c->IoReady = 1;
    return result;
}

int hv_agx_retained_io_manifest(struct hv_agx_retained_root *c, unsigned long long epoch,
                               AGX_FW_IO_MANIFEST *out)
{
    AGX_FW_IO_MANIFEST manifest = {0};
    unsigned int slot;
    int result;
    if (!out)
        return HV_AGX_RETAINED_INVALID;
    *out = manifest;
    result = active_epoch(c, epoch);
    if (result)
        return result;
    if (!c->IoReady)
        return HV_AGX_RETAINED_STATE;
    result = check_io(c, 1);
    if (result)
        return result;
    manifest.Magic = AGX_FW_IO_MAGIC;
    manifest.Version = AGX_FW_IO_VERSION;
    manifest.Bytes = AGX_FW_IO_BYTES;
    manifest.Chip = 0x8103;
    manifest.Epoch = c->Epoch;
    manifest.Root = c->RetainedPa;
    manifest.Ready = 1;
    manifest.Count = AGX_FW_IO_SLOTS;
    for (slot = 0; slot < AGX_FW_IO_SLOTS; ++slot)
        (void)AgxFwIoProfile(slot, &manifest.Records[slot]);
    *out = manifest;
    return HV_AGX_RETAINED_OK;
}

int hv_agx_retained_query(struct hv_agx_retained_root *c, unsigned long long epoch,
                         unsigned long long handle, unsigned long long va,
                         unsigned long long ipa, unsigned long long length,
                         unsigned long long *pa_out)
{
    struct hv_agx_retained_mapping *m;
    int result;
    if (!pa_out)
        return HV_AGX_RETAINED_INVALID;
    *pa_out = 0;
    result = active_epoch(c, epoch);
    if (result)
        return result;
    m = find_mapping(c, handle, va, ipa, length);
    if (!m)
        return HV_AGX_RETAINED_OWNERSHIP;
    result = check_leaf(
        c, va, m->Pa,
        va == J313_AGX_G2_REGIONB_BUFFER_MGR_GPU_VA
            ? AppleAgxUatFirmwareGpuSharedReadWrite
            : AppleAgxUatFirmwareSharedReadWrite);
    if (result)
        return result;
    *pa_out = m->Pa;
    return HV_AGX_RETAINED_OK;
}

int hv_agx_retained_unmap(struct hv_agx_retained_root *c, unsigned long long epoch,
                         unsigned long long handle, unsigned long long va,
                         unsigned long long ipa, unsigned long long length)
{
    struct hv_agx_retained_mapping *m;
    struct hv_agx_retained_mapping removed;
    unsigned long long pa;
    int result = hv_agx_retained_query(c, epoch, handle, va, ipa, length, &pa);
    if (result)
        return result;
    m = find_mapping(c, handle, va, ipa, length);
    removed = *m;
    if (AppleAgxUatUnmap(0, &c->Roots, va, length, &c->Allocator, &c->Inventory) !=
        AppleAgxUatResultOk) {
        c->Tainted = 1;
        return HV_AGX_RETAINED_TAINTED;
    }
    *m = c->Mappings[--c->MappingCount];
    c->Mappings[c->MappingCount] = (struct hv_agx_retained_mapping){0};
    c->Ops.Sync(c->Ops.Context);
    result = check_tables(c);
    if (!result)
        c->LastUnmap = removed;
    return result;
}

int hv_agx_retained_verify_absent(struct hv_agx_retained_root *c,
                                 unsigned long long epoch,
                                 unsigned long long handle, unsigned long long va,
                                 unsigned long long ipa, unsigned long long length)
{
    unsigned int i;
    unsigned long long *leaf;
    int result = active_epoch(c, epoch);
    if (result)
        return result;
    if (!handle || c->LastUnmap.Handle != handle || c->LastUnmap.Va != va ||
        c->LastUnmap.Ipa != ipa || c->LastUnmap.Length != length)
        return HV_AGX_RETAINED_OWNERSHIP;
    for (i = 0; i < c->MappingCount; ++i)
        if (c->Mappings[i].Va == va)
            return HV_AGX_RETAINED_OWNERSHIP;
    result = owned_leaf(c, va, &leaf);
    if (result)
        return result;
    return !leaf || !*leaf ? HV_AGX_RETAINED_OK : HV_AGX_RETAINED_OWNERSHIP;
}

int hv_agx_retained_close(struct hv_agx_retained_root *c, unsigned long long epoch,
                         unsigned char cpu_stopped)
{
    unsigned int slot;
    int result;
    if (!c)
        return HV_AGX_RETAINED_INVALID;
    if (!epoch || c->Epoch != epoch || !cpu_stopped)
        return HV_AGX_RETAINED_STATE;
    if (c->Tainted)
        return HV_AGX_RETAINED_TAINTED;
    if (!c->Prepared)
        return HV_AGX_RETAINED_OK;
    result = check_tables(c);
    if (result)
        return result;
    if (c->SystemBytes) {
        result = check_leaf(c, c->SystemVa, c->SystemPage.PhysicalAddress,
                            AppleAgxUatFirmwarePrivateReadWrite);
        if (result)
            return result;
    }
    result = check_io(c, c->IoReady);
    if (result)
        return result;
    while (c->MappingCount) {
        struct hv_agx_retained_mapping m = c->Mappings[c->MappingCount - 1];
        result = hv_agx_retained_unmap(c, epoch, m.Handle, m.Va, m.Ipa, m.Length);
        if (result)
            return result;
    }
    for (slot = AGX_FW_IO_SLOTS; slot-- > 0;) {
        AGX_FW_IO_DESCRIPTOR d;
        if (!(c->IoMappedSlots & (1u << slot)))
            continue;
        (void)AgxFwIoProfile(slot, &d);
        if (AppleAgxUatUnmap(0, &c->Roots, d.Virt & ~(PAGE_BYTES - 1), io_length(&d),
                            &c->Allocator, &c->Inventory) != AppleAgxUatResultOk) {
            c->Tainted = 1;
            return HV_AGX_RETAINED_TAINTED;
        }
        c->IoMappedSlots &= ~(1u << slot);
        c->Ops.Sync(c->Ops.Context);
    }
    if (c->SystemBytes) {
        if (AppleAgxUatUnmap(0, &c->Roots, c->SystemVa, c->SystemBytes,
                            &c->Allocator, &c->Inventory) != AppleAgxUatResultOk) {
            c->Tainted = 1;
            return HV_AGX_RETAINED_TAINTED;
        }
    }
    /* Unmap prunes owned children and their slot-0/slot-2 parents. Recheck ownership
     * before detaching any residual owned empty subtree. Never follow it. */
    result = check_tables(c);
    if (result)
        return result;
    c->RetainedEntries[2] = 0;
    owned_child(c, c->Roots.Ttbr0PhysicalAddress | 3ULL, 0)->Entries[0] = 0;
    c->Ops.Sync(c->Ops.Context);
    if (c->PrefixSaved && !hv_agx_retained_prefix_unchanged(c))
        return HV_AGX_RETAINED_TAINTED;
    AppleAgxUatDestroy(&c->Allocator, &c->Inventory);
    if (c->SystemPage.Entries)
        release_page(c, &c->SystemPage);
    c->SystemPage = (APPLE_AGX_UAT_PAGE){0};
    c->SystemVa = c->SystemBytes = 0;
    c->Active = c->Prepared = 0;
    c->LastUnmap = (struct hv_agx_retained_mapping){0};
    c->IoAttempted = c->IoReady = 0;
    return HV_AGX_RETAINED_OK;
}
