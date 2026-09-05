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

/* Only slot 2 can point to an owned kernel subtree. No walk of private slots
 * or a foreign descriptor is permitted, even during cleanup. */
static int check_tables(struct hv_agx_retained_root *c)
{
    unsigned int i;
    unsigned long long slot;
    if (c->Tainted || (c->PrefixSaved && !hv_agx_retained_prefix_unchanged(c)))
        return HV_AGX_RETAINED_TAINTED;
    if (c->Roots.Ttbr1PhysicalAddress != c->RetainedPa)
        goto tainted;
    for (i = 3; i < 2048; ++i)
        if (c->RetainedEntries[i])
            goto tainted;
    slot = c->RetainedEntries[2];
    if (!slot)
        return HV_AGX_RETAINED_OK;
    for (i = 0; i < c->Inventory.PageCount; ++i)
        if (c->Pages[i].Level == 1 &&
            slot == (c->Pages[i].PhysicalAddress | 3ULL) &&
            !reserved_pa(c, c->Pages[i].PhysicalAddress))
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
        core->UatMappings, HV_AGX_RETAINED_MAX_MAPPINGS + 1, 0};
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
        va < HV_AGX_RETAINED_WINDOWS_VA || va >= HV_AGX_RETAINED_WINDOWS_END)
        return HV_AGX_RETAINED_RANGE;
    for (i = 0; i < c->MappingCount; ++i)
        if (c->Mappings[i].Va == va)
            return HV_AGX_RETAINED_OWNERSHIP;
    if (c->MappingCount == HV_AGX_RETAINED_MAX_MAPPINGS || !c->NextHandle)
        return HV_AGX_RETAINED_ALLOCATION;
    pa = c->Ops.TranslateGuest(c->Ops.Context, ipa);
    if (!valid_pa(pa) || reserved_pa(c, pa) || owned_pa(c, pa))
        return HV_AGX_RETAINED_RANGE;
    mapped = AppleAgxUatMap(0, &c->Roots, va, pa, length,
        AppleAgxUatFirmwareSharedReadWrite, &c->Allocator, &c->Inventory);
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

static int check_leaf(struct hv_agx_retained_root *c, unsigned long long va,
                      unsigned long long expected_pa,
                      APPLE_AGX_UAT_PROTECTION protection)
{
    unsigned long long pa, descriptor, expected;
    if (AppleAgxUatResolvePage(0, &c->Roots, va, &c->Inventory, &pa, &descriptor) !=
        AppleAgxUatResultOk || pa != expected_pa ||
        AppleAgxUatEncodePageDescriptor(0, expected_pa, protection, &expected) !=
        AppleAgxUatResultOk || descriptor != expected) {
        c->Tainted = 1;
        return HV_AGX_RETAINED_TAINTED;
    }
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
    result = check_leaf(c, va, m->Pa, AppleAgxUatFirmwareSharedReadWrite);
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
    unsigned long long pa;
    int result = hv_agx_retained_query(c, epoch, handle, va, ipa, length, &pa);
    if (result)
        return result;
    m = find_mapping(c, handle, va, ipa, length);
    if (AppleAgxUatUnmap(0, &c->Roots, va, length, &c->Allocator, &c->Inventory) !=
        AppleAgxUatResultOk) {
        c->Tainted = 1;
        return HV_AGX_RETAINED_TAINTED;
    }
    *m = c->Mappings[--c->MappingCount];
    c->Mappings[c->MappingCount] = (struct hv_agx_retained_mapping){0};
    c->Ops.Sync(c->Ops.Context);
    return check_tables(c);
}

int hv_agx_retained_close(struct hv_agx_retained_root *c, unsigned long long epoch,
                         unsigned char cpu_stopped)
{
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
    while (c->MappingCount) {
        struct hv_agx_retained_mapping m = c->Mappings[c->MappingCount - 1];
        result = hv_agx_retained_unmap(c, epoch, m.Handle, m.Va, m.Ipa, m.Length);
        if (result)
            return result;
    }
    if (c->SystemBytes) {
        if (AppleAgxUatUnmap(0, &c->Roots, c->SystemVa, c->SystemBytes,
                            &c->Allocator, &c->Inventory) != AppleAgxUatResultOk) {
            c->Tainted = 1;
            return HV_AGX_RETAINED_TAINTED;
        }
    }
    /* Unmap prunes owned children and their slot-2 parent. Recheck ownership
     * before detaching any residual owned empty subtree. Never follow it. */
    result = check_tables(c);
    if (result)
        return result;
    c->RetainedEntries[2] = 0;
    c->Ops.Sync(c->Ops.Context);
    if (c->PrefixSaved && !hv_agx_retained_prefix_unchanged(c))
        return HV_AGX_RETAINED_TAINTED;
    AppleAgxUatDestroy(&c->Allocator, &c->Inventory);
    if (c->SystemPage.Entries)
        release_page(c, &c->SystemPage);
    c->SystemPage = (APPLE_AGX_UAT_PAGE){0};
    c->SystemVa = c->SystemBytes = 0;
    c->Active = c->Prepared = 0;
    return HV_AGX_RETAINED_OK;
}
