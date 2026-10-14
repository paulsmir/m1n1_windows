/* SPDX-License-Identifier: MIT */
#include "../src/hv_agx_pool_window.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define PG AGX_POOL_WINDOW_PAGE
#define RAM_BASE UINT64_C(0x850000000)
#define RAM_BYTES UINT64_C(0x10000000) /* 256 MiB fake guest RAM */
#define WIN_BASE UINT64_C(0xd00000000)
#define WIN_BYTES UINT64_C(0x400000) /* 4 MiB, 256 pages */
#define WIN_PAGES (WIN_BYTES / PG)

static uint64_t stage[WIN_PAGES];
static unsigned map_calls, sync_calls, sync_invalidated, fail_map_after;
static uint64_t busy_pa, refuse_ipa;

static uint64_t fake_translate(void *c, uint64_t ipa)
{
    (void)c;
    if (ipa == refuse_ipa)
        return 0;
    /* A low alias like m1n1's low window: IPA 0x100000.. -> RAM_BASE + 0x8000000.. */
    if (ipa >= 0x100000 && ipa < 0x100000 + 0x1000000)
        return RAM_BASE + 0x8000000 + (ipa - 0x100000);
    if (ipa >= RAM_BASE && ipa < RAM_BASE + RAM_BYTES)
        return ipa;
    return 0;
}

static bool fake_map(void *c, uint64_t window_ipa, uint64_t pa)
{
    (void)c;
    assert(window_ipa >= WIN_BASE && window_ipa < WIN_BASE + WIN_BYTES &&
           !(window_ipa & (PG - 1)));
    if (pa && fail_map_after && map_calls + 1 == fail_map_after) {
        ++map_calls;
        return false;
    }
    ++map_calls;
    stage[(window_ipa - WIN_BASE) / PG] = pa;
    return true;
}

static void fake_sync(void *c, bool invalidated)
{
    (void)c;
    ++sync_calls;
    if (invalidated)
        ++sync_invalidated;
}

static bool fake_busy(void *c, uint64_t pa)
{
    (void)c;
    return busy_pa && pa == busy_pa;
}

static const struct hv_agx_pool_window_ops ops = {NULL, fake_translate, fake_map,
                                                  fake_sync, fake_busy};

static struct hv_agx_pool_window w;
static uint64_t *pages, *present;

static void reset(void)
{
    memset(stage, 0, sizeof(stage));
    map_calls = sync_calls = sync_invalidated = fail_map_after = 0;
    busy_pa = refuse_ipa = 0;
    memset(pages, 0, hv_agx_pool_window_pages_bytes(WIN_BYTES));
    memset(present, 0, hv_agx_pool_window_present_bytes(RAM_BYTES));
    assert(hv_agx_pool_window_init(&w, WIN_BASE, WIN_BYTES, RAM_BASE, RAM_BYTES,
                                   pages, present, &ops));
}

static unsigned mapped_entries(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < WIN_PAGES; ++i)
        n += stage[i] != 0;
    return n;
}

static void test_init_rejects_bad_windows(void)
{
    struct hv_agx_pool_window x;
    struct hv_agx_pool_window_ops no_busy = ops;
    no_busy.busy = NULL;
    /* Overlapping guest RAM, misaligned, empty, missing ops or storage. */
    assert(!hv_agx_pool_window_init(&x, RAM_BASE + RAM_BYTES - PG, WIN_BYTES, RAM_BASE,
                                    RAM_BYTES, pages, present, &ops));
    assert(!hv_agx_pool_window_init(&x, WIN_BASE + 0x1000, WIN_BYTES, RAM_BASE,
                                    RAM_BYTES, pages, present, &ops));
    assert(!hv_agx_pool_window_init(&x, WIN_BASE, 0, RAM_BASE, RAM_BYTES, pages,
                                    present, &ops));
    assert(!hv_agx_pool_window_init(&x, WIN_BASE, WIN_BYTES, RAM_BASE, RAM_BYTES,
                                    pages, present, &no_busy));
    assert(!hv_agx_pool_window_init(&x, WIN_BASE, WIN_BYTES, RAM_BASE, RAM_BYTES,
                                    NULL, present, &ops));
    /* An unavailable window answers State and reports no range. */
    assert(!x.available);
    assert(hv_agx_pool_window_map_append(&x, RAM_BASE, PG) == AgxPoolWindowState);
    assert(hv_agx_pool_window_unmap_all(&x) == AgxPoolWindowState);
    uint64_t v = 1;
    assert(hv_agx_pool_window_mmio(&x, AGX_POOL_WINDOW_REG_STATUS, &v, false, 2) && v == 0);
    assert(hv_agx_pool_window_mmio(&x, AGX_POOL_WINDOW_REG_IPA, &v, false, 3) && v == 0);
}

static void test_append_in_order(void)
{
    reset();
    /* Two scattered runs (one through the low alias) become one window. */
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE + 0x40000, 0x10000) == AgxPoolWindowOk);
    assert(hv_agx_pool_window_map_append(&w, 0x100000 + 0x20000, 0x8000) == AgxPoolWindowOk);
    assert(w.mapped == 0x18000 && mapped_entries() == 6);
    for (unsigned i = 0; i < 4; ++i)
        assert(stage[i] == RAM_BASE + 0x40000 + i * PG);
    assert(stage[4] == RAM_BASE + 0x8000000 + 0x20000 && stage[5] == stage[4] + PG);
    assert(sync_calls == 2 && sync_invalidated == 0);
}

static void test_refusals_leave_no_trace(void)
{
    reset();
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE, 0x8000) == AgxPoolWindowOk);
    unsigned calls = map_calls;
    /* Overlapping the mapped run (cross-request) and inside one request. */
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE + PG, 0x8000) ==
           AgxPoolWindowDuplicate);
    /* The low alias resolving to an already mapped PA is a duplicate too. */
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE + 0x8000000, 0x8000) == AgxPoolWindowOk);
    calls = map_calls;
    assert(hv_agx_pool_window_map_append(&w, 0x100000 + PG, PG) == AgxPoolWindowDuplicate);
    refuse_ipa = RAM_BASE + 0x204000;
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE + 0x200000, 0x10000) ==
           AgxPoolWindowPage);
    refuse_ipa = 0;
    /* Outside guest RAM, or inside the window itself. */
    assert(hv_agx_pool_window_map_append(&w, 0x780000000, PG) == AgxPoolWindowPage);
    assert(hv_agx_pool_window_map_append(&w, WIN_BASE + 0x100000, PG) == AgxPoolWindowPage);
    busy_pa = RAM_BASE + 0x30c000;
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE + 0x300000, 0x10000) ==
           AgxPoolWindowBusy);
    busy_pa = 0;
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE + 0x1000, PG) == AgxPoolWindowInvalid);
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE, 0) == AgxPoolWindowInvalid);
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE + 0x400000, WIN_BYTES) ==
           AgxPoolWindowCapacity);
    assert(map_calls == calls && w.mapped == 0x10000 && mapped_entries() == 4);
    /* Refused pages were released: the same pages map afterwards. */
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE + 0x200000, 0x10000) == AgxPoolWindowOk);
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE + 0x300000, 0x10000) == AgxPoolWindowOk);
    assert(w.mapped == 0x30000);
}

static void test_stage2_failure_rolls_back(void)
{
    reset();
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE, PG) == AgxPoolWindowOk);
    fail_map_after = map_calls + 3;
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE + 0x10000, 0x10000) ==
           AgxPoolWindowState);
    assert(w.mapped == PG && mapped_entries() == 1 && sync_invalidated == 1);
    fail_map_after = 0;
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE + 0x10000, 0x10000) == AgxPoolWindowOk);
    assert(stage[1] == RAM_BASE + 0x10000 && w.mapped == 0x14000);
}

static void test_unmap_all(void)
{
    reset();
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE, 0x10000) == AgxPoolWindowOk);
    busy_pa = RAM_BASE + 0x8000;
    unsigned calls = map_calls;
    assert(hv_agx_pool_window_unmap_all(&w) == AgxPoolWindowBusy);
    assert(map_calls == calls && w.mapped == 0x10000 && mapped_entries() == 4);
    busy_pa = 0;
    assert(hv_agx_pool_window_unmap_all(&w) == AgxPoolWindowOk);
    assert(w.mapped == 0 && mapped_entries() == 0 && sync_invalidated == 1);
    /* Released pages map again from the window base. */
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE, 0x10000) == AgxPoolWindowOk);
    assert(stage[0] == RAM_BASE && w.mapped == 0x10000);
    /* An empty window unmaps without stage-2 maintenance. */
    assert(hv_agx_pool_window_unmap_all(&w) == AgxPoolWindowOk);
    unsigned syncs = sync_calls;
    assert(hv_agx_pool_window_unmap_all(&w) == AgxPoolWindowOk && sync_calls == syncs);
}

static void test_mmio(void)
{
    uint64_t v;
    reset();
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_MAGIC, &v, false, 2) &&
           v == AGX_POOL_WINDOW_MAGIC);
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_VERSION, &v, false, 2) &&
           v == AGX_POOL_WINDOW_VERSION);
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_STATUS, &v, false, 2) &&
           v == (AGX_POOL_WINDOW_STATUS_AVAILABLE | AGX_POOL_WINDOW_STATUS_RANGES));
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_IPA, &v, false, 3) && v == WIN_BASE);
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_CAPACITY, &v, false, 3) &&
           v == WIN_BYTES);
    /* Wrong widths and unknown offsets are not part of the ABI. */
    assert(!hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_IPA, &v, false, 2));
    assert(!hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_MAGIC, &v, false, 3));
    assert(!hv_agx_pool_window_mmio(&w, 0x0c, &v, false, 2));
    v = AGX_POOL_WINDOW_CMD_MAP_APPEND;
    assert(!hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_MAGIC, &v, true, 2));
    v = RAM_BASE + 0x80000;
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_SOURCE_IPA, &v, true, 3));
    v = 0x20000;
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_SOURCE_BYTES, &v, true, 3));
    v = AGX_POOL_WINDOW_CMD_MAP_APPEND;
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_COMMAND, &v, true, 2));
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_RESULT, &v, false, 2) &&
           v == AgxPoolWindowOk);
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_MAPPED, &v, false, 3) &&
           v == 0x20000);
    assert(stage[0] == RAM_BASE + 0x80000 && stage[7] == RAM_BASE + 0x9c000);
    v = AGX_POOL_WINDOW_CMD_MAP_APPEND; /* same source again: duplicate */
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_COMMAND, &v, true, 2));
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_RESULT, &v, false, 2) &&
           v == AgxPoolWindowDuplicate);
    v = 7;
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_COMMAND, &v, true, 2));
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_RESULT, &v, false, 2) &&
           v == AgxPoolWindowInvalid);
    v = AGX_POOL_WINDOW_CMD_UNMAP_ALL;
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_COMMAND, &v, true, 2));
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_MAPPED, &v, false, 3) && v == 0);
}

static uint64_t mmio_command(uint32_t command, uint64_t window_offset, uint64_t source_ipa,
                             uint64_t bytes)
{
    uint64_t v = window_offset;
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_WINDOW_OFFSET, &v, true, 3));
    v = source_ipa;
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_SOURCE_IPA, &v, true, 3));
    v = bytes;
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_SOURCE_BYTES, &v, true, 3));
    v = command;
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_COMMAND, &v, true, 2));
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_RESULT, &v, false, 2));
    return v;
}

/* M2-f3: commit and decommit arbitrary window ranges (demand-driven segment 3). */
static void test_map_at_and_unmap_range(void)
{
    reset();
    /* Commit two ranges far apart; the hole between them stays invalid. */
    assert(hv_agx_pool_window_map_at(&w, 0x100000, RAM_BASE + 0x40000, 0x10000) ==
           AgxPoolWindowOk);
    assert(hv_agx_pool_window_map_at(&w, 0x10000, 0x100000, 0x8000) == AgxPoolWindowOk);
    assert(w.sparse && w.mapped == 0x18000 && mapped_entries() == 6);
    assert(stage[0x100000 / PG] == RAM_BASE + 0x40000 && stage[0x10000 / PG + 1] ==
           RAM_BASE + 0x8000000 + PG);
    assert(stage[0] == 0 && stage[0x10000 / PG + 2] == 0);
    /* A sparse window refuses appends until UNMAP_ALL. */
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE + 0x200000, PG) == AgxPoolWindowState);
    unsigned calls = map_calls;
    /* Mapping over a committed page, partially or exactly, is refused. */
    assert(hv_agx_pool_window_map_at(&w, 0x10000 - PG, RAM_BASE + 0x200000, 0x8000) ==
           AgxPoolWindowOccupied);
    assert(hv_agx_pool_window_map_at(&w, 0x100000, RAM_BASE + 0x200000, PG) ==
           AgxPoolWindowOccupied);
    /* The source invariants hold for MAP_AT as for MAP_APPEND. */
    assert(hv_agx_pool_window_map_at(&w, 0x200000, RAM_BASE + 0x4c000, PG) ==
           AgxPoolWindowDuplicate);
    busy_pa = RAM_BASE + 0x204000;
    assert(hv_agx_pool_window_map_at(&w, 0x200000, RAM_BASE + 0x200000, 0x8000) ==
           AgxPoolWindowBusy);
    busy_pa = 0;
    assert(hv_agx_pool_window_map_at(&w, 0x200000, WIN_BASE, PG) == AgxPoolWindowPage);
    assert(hv_agx_pool_window_map_at(&w, 0x2000, RAM_BASE, PG) == AgxPoolWindowInvalid);
    assert(hv_agx_pool_window_map_at(&w, WIN_BYTES - PG, RAM_BASE, 0x8000) ==
           AgxPoolWindowCapacity);
    assert(hv_agx_pool_window_map_at(&w, WIN_BYTES, RAM_BASE, PG) == AgxPoolWindowCapacity);
    assert(map_calls == calls && w.mapped == 0x18000 && mapped_entries() == 6);
    /* The last window page is committable. */
    assert(hv_agx_pool_window_map_at(&w, WIN_BYTES - PG, RAM_BASE + 0x300000, PG) ==
           AgxPoolWindowOk);
    assert(stage[WIN_PAGES - 1] == RAM_BASE + 0x300000);

    /* Decommit: a range with an uncommitted page or a broker-held page is refused. */
    calls = map_calls;
    assert(hv_agx_pool_window_unmap_range(&w, 0x10000, 0xc000) == AgxPoolWindowOccupied);
    busy_pa = RAM_BASE + 0x44000;
    assert(hv_agx_pool_window_unmap_range(&w, 0x100000, 0x10000) == AgxPoolWindowBusy);
    busy_pa = 0;
    assert(hv_agx_pool_window_unmap_range(&w, 0x100000, 0) == AgxPoolWindowInvalid);
    assert(hv_agx_pool_window_unmap_range(&w, WIN_BYTES, PG) == AgxPoolWindowCapacity);
    assert(map_calls == calls && mapped_entries() == 7);
    /* A sub-range decommits alone and invalidates the stage-2 TLBs. */
    unsigned invalidated = sync_invalidated;
    assert(hv_agx_pool_window_unmap_range(&w, 0x104000, 0x8000) == AgxPoolWindowOk);
    assert(sync_invalidated == invalidated + 1 && w.mapped == 0x14000 &&
           mapped_entries() == 5);
    assert(stage[0x100000 / PG] && !stage[0x104000 / PG] && !stage[0x108000 / PG] &&
           stage[0x10c000 / PG]);
    /* Its source pages are free again: they commit elsewhere in the window. */
    assert(hv_agx_pool_window_map_at(&w, 0x300000, RAM_BASE + 0x44000, 0x8000) ==
           AgxPoolWindowOk);
    assert(stage[0x300000 / PG] == RAM_BASE + 0x44000);

    /* UNMAP_ALL releases every committed page, not only a prefix. */
    busy_pa = RAM_BASE + 0x300000; /* the last window page */
    assert(hv_agx_pool_window_unmap_all(&w) == AgxPoolWindowBusy);
    busy_pa = 0;
    assert(hv_agx_pool_window_unmap_all(&w) == AgxPoolWindowOk);
    assert(!w.sparse && w.mapped == 0 && mapped_entries() == 0);
    /* Appends work again from the base; every source page is reusable. */
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE + 0x40000, 0x10000) == AgxPoolWindowOk);
    assert(hv_agx_pool_window_map_append(&w, RAM_BASE + 0x300000, PG) == AgxPoolWindowOk);
    assert(w.mapped == 0x14000 && stage[4] == RAM_BASE + 0x300000);
}

static void test_range_mmio(void)
{
    uint64_t v;
    reset();
    assert(mmio_command(AGX_POOL_WINDOW_CMD_MAP_AT, 0x40000, RAM_BASE + 0x80000, 0x8000) ==
           AgxPoolWindowOk);
    assert(stage[0x40000 / PG] == RAM_BASE + 0x80000 && stage[0x44000 / PG] ==
           RAM_BASE + 0x84000);
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_WINDOW_OFFSET, &v, false, 3) &&
           v == 0x40000);
    assert(hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_MAPPED, &v, false, 3) &&
           v == 0x8000);
    /* WINDOW_OFFSET is a 64-bit register. */
    assert(!hv_agx_pool_window_mmio(&w, AGX_POOL_WINDOW_REG_WINDOW_OFFSET, &v, true, 2));
    assert(mmio_command(AGX_POOL_WINDOW_CMD_MAP_APPEND, 0, RAM_BASE, PG) ==
           AgxPoolWindowState);
    assert(mmio_command(AGX_POOL_WINDOW_CMD_UNMAP_RANGE, 0x40000, 0, 0xc000) ==
           AgxPoolWindowOccupied);
    assert(mmio_command(AGX_POOL_WINDOW_CMD_UNMAP_RANGE, 0x44000, 0, PG) == AgxPoolWindowOk);
    assert(mmio_command(AGX_POOL_WINDOW_CMD_UNMAP_RANGE, 0x40000, 0, PG) == AgxPoolWindowOk);
    assert(mapped_entries() == 0 && w.mapped == 0);
    /* An empty sparse window still needs UNMAP_ALL before appends. */
    assert(mmio_command(AGX_POOL_WINDOW_CMD_MAP_APPEND, 0, RAM_BASE, PG) ==
           AgxPoolWindowState);
    assert(mmio_command(AGX_POOL_WINDOW_CMD_UNMAP_ALL, 0, 0, 0) == AgxPoolWindowOk);
    assert(mmio_command(AGX_POOL_WINDOW_CMD_MAP_APPEND, 0, RAM_BASE, PG) == AgxPoolWindowOk);
}

int main(void)
{
    pages = calloc(1, hv_agx_pool_window_pages_bytes(WIN_BYTES));
    present = calloc(1, hv_agx_pool_window_present_bytes(RAM_BYTES));
    assert(pages && present);
    test_init_rejects_bad_windows();
    test_append_in_order();
    test_refusals_leave_no_trace();
    test_stage2_failure_rolls_back();
    test_unmap_all();
    test_mmio();
    test_map_at_and_unmap_range();
    test_range_mmio();
    free(pages);
    free(present);
    return 0;
}
