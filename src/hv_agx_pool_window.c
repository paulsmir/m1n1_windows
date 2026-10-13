/* SPDX-License-Identifier: MIT */
#include "hv_agx_pool_window.h"

#define PAGE AGX_POOL_WINDOW_PAGE

uint64_t hv_agx_pool_window_pages_bytes(uint64_t capacity)
{
    return capacity / PAGE * sizeof(uint64_t);
}

uint64_t hv_agx_pool_window_present_bytes(uint64_t ram_bytes)
{
    return (ram_bytes / PAGE + 63u) / 64u * sizeof(uint64_t);
}

static bool page_present(const struct hv_agx_pool_window *w, uint64_t pa)
{
    uint64_t bit = (pa - w->ram_base) / PAGE;
    return (w->present[bit / 64u] >> (bit % 64u)) & 1u;
}

static void page_mark(struct hv_agx_pool_window *w, uint64_t pa, bool present)
{
    uint64_t bit = (pa - w->ram_base) / PAGE;
    uint64_t mask = UINT64_C(1) << (bit % 64u);
    if (present)
        w->present[bit / 64u] |= mask;
    else
        w->present[bit / 64u] &= ~mask;
}

bool hv_agx_pool_window_init(struct hv_agx_pool_window *w, uint64_t base,
                             uint64_t capacity, uint64_t ram_base,
                             uint64_t ram_bytes, uint64_t *pages,
                             uint64_t *present,
                             const struct hv_agx_pool_window_ops *ops)
{
    if (!w)
        return false;
    *w = (struct hv_agx_pool_window){0};
    if (!ops || !ops->translate || !ops->map || !ops->sync || !ops->busy ||
        !pages || !present || !capacity || !ram_bytes ||
        ((base | capacity | ram_base | ram_bytes) & (PAGE - 1)) ||
        base > UINT64_MAX - capacity || ram_base > UINT64_MAX - ram_bytes)
        return false;
    /* The window aliases RAM; it must never lie inside it. */
    if (base < ram_base + ram_bytes && ram_base < base + capacity)
        return false;
    w->ops = *ops;
    w->base = base;
    w->capacity = capacity;
    w->ram_base = ram_base;
    w->ram_bytes = ram_bytes;
    w->pages = pages;
    w->present = present;
    w->available = true;
    return true;
}

enum AGX_POOL_WINDOW_RESULT hv_agx_pool_window_map_append(
    struct hv_agx_pool_window *w, uint64_t source_ipa, uint64_t bytes)
{
    uint64_t first, count, i, j;
    enum AGX_POOL_WINDOW_RESULT result = AgxPoolWindowOk;

    if (!w || !w->available)
        return AgxPoolWindowState;
    if (!bytes || ((source_ipa | bytes) & (PAGE - 1)) ||
        source_ipa > UINT64_MAX - bytes)
        return AgxPoolWindowInvalid;
    if (bytes > w->capacity - w->mapped)
        return AgxPoolWindowCapacity;
    if (source_ipa < w->base + w->capacity && w->base < source_ipa + bytes)
        return AgxPoolWindowPage;
    first = w->mapped / PAGE;
    count = bytes / PAGE;
    for (i = 0; i < count; ++i) {
        uint64_t pa = w->ops.translate(w->ops.context, source_ipa + i * PAGE);
        if (!pa || (pa & (PAGE - 1)) || pa < w->ram_base ||
            pa - w->ram_base >= w->ram_bytes) {
            result = AgxPoolWindowPage;
            break;
        }
        if (page_present(w, pa)) {
            result = AgxPoolWindowDuplicate;
            break;
        }
        if (w->ops.busy(w->ops.context, pa)) {
            result = AgxPoolWindowBusy;
            break;
        }
        page_mark(w, pa, true);
        w->pages[first + i] = pa;
    }
    if (result == AgxPoolWindowOk) {
        for (j = 0; j < count; ++j)
            if (!w->ops.map(w->ops.context, w->base + (first + j) * PAGE,
                            w->pages[first + j]))
                break;
        if (j == count) {
            w->ops.sync(w->ops.context, false);
            w->mapped += bytes;
            return AgxPoolWindowOk;
        }
        /* A stage-2 failure leaves no partial append behind. */
        while (j--)
            (void)w->ops.map(w->ops.context, w->base + (first + j) * PAGE, 0);
        w->ops.sync(w->ops.context, true);
        result = AgxPoolWindowState;
        i = count;
    }
    while (i--) {
        page_mark(w, w->pages[first + i], false);
        w->pages[first + i] = 0;
    }
    return result;
}

enum AGX_POOL_WINDOW_RESULT hv_agx_pool_window_unmap_all(
    struct hv_agx_pool_window *w)
{
    uint64_t count, i;
    bool failed = false;

    if (!w || !w->available)
        return AgxPoolWindowState;
    count = w->mapped / PAGE;
    for (i = 0; i < count; ++i)
        if (w->ops.busy(w->ops.context, w->pages[i]))
            return AgxPoolWindowBusy;
    for (i = 0; i < count; ++i)
        if (!w->ops.map(w->ops.context, w->base + i * PAGE, 0))
            failed = true;
    if (count)
        w->ops.sync(w->ops.context, true);
    if (failed)
        return AgxPoolWindowState;
    for (i = 0; i < count; ++i) {
        page_mark(w, w->pages[i], false);
        w->pages[i] = 0;
    }
    w->mapped = 0;
    return AgxPoolWindowOk;
}

bool hv_agx_pool_window_mmio(struct hv_agx_pool_window *w, uint64_t offset,
                             uint64_t *value, bool write, unsigned width)
{
    if (!w || !value)
        return false;
    if (width == 2) {
        if (write) {
            if (offset != AGX_POOL_WINDOW_REG_COMMAND)
                return false;
            if (*value == AGX_POOL_WINDOW_CMD_MAP_APPEND)
                w->result = hv_agx_pool_window_map_append(w, w->source_ipa,
                                                          w->source_bytes);
            else if (*value == AGX_POOL_WINDOW_CMD_UNMAP_ALL)
                w->result = hv_agx_pool_window_unmap_all(w);
            else
                w->result = AgxPoolWindowInvalid;
            return true;
        }
        switch (offset) {
        case AGX_POOL_WINDOW_REG_MAGIC:
            *value = AGX_POOL_WINDOW_MAGIC;
            return true;
        case AGX_POOL_WINDOW_REG_VERSION:
            *value = AGX_POOL_WINDOW_VERSION;
            return true;
        case AGX_POOL_WINDOW_REG_STATUS:
            *value = w->available ? AGX_POOL_WINDOW_STATUS_AVAILABLE : 0u;
            return true;
        case AGX_POOL_WINDOW_REG_RESULT:
            *value = w->result;
            return true;
        default:
            return false;
        }
    }
    if (width != 3)
        return false;
    if (write) {
        if (offset == AGX_POOL_WINDOW_REG_SOURCE_IPA)
            w->source_ipa = *value;
        else if (offset == AGX_POOL_WINDOW_REG_SOURCE_BYTES)
            w->source_bytes = *value;
        else
            return false;
        return true;
    }
    switch (offset) {
    case AGX_POOL_WINDOW_REG_IPA:
        *value = w->available ? w->base : 0u;
        return true;
    case AGX_POOL_WINDOW_REG_CAPACITY:
        *value = w->available ? w->capacity : 0u;
        return true;
    case AGX_POOL_WINDOW_REG_MAPPED:
        *value = w->mapped;
        return true;
    case AGX_POOL_WINDOW_REG_SOURCE_IPA:
        *value = w->source_ipa;
        return true;
    case AGX_POOL_WINDOW_REG_SOURCE_BYTES:
        *value = w->source_bytes;
        return true;
    default:
        return false;
    }
}
