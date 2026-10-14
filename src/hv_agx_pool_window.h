/* SPDX-License-Identifier: MIT */
#ifndef HV_AGX_POOL_WINDOW_H
#define HV_AGX_POOL_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include "../../drivers/apple-agx/shared/include/apple_agx_pool_window_abi.h"

/* M2-f2: one contiguous guest-IPA window over guest RAM pages the Windows
 * KMD allocated (its segment 3). Invariants: every mapped window page maps one
 * plain guest RAM page, no PA twice, and no page leaves the window while the
 * GPU broker holds a backing for its PA. The window outlives broker epochs. */
struct hv_agx_pool_window_ops {
    void *context;
    /* 16 KiB-aligned guest page -> PA when it is plain guest RAM a window may
     * alias (not firmware, reserve, MMIO or the window itself); 0 otherwise. */
    uint64_t (*translate)(void *context, uint64_t ipa);
    /* Stage-2 entry of one window page; pa 0 makes it invalid. */
    bool (*map)(void *context, uint64_t window_ipa, uint64_t pa);
    /* Publishes entry stores; after invalidations also drops stage-2 TLBs. */
    void (*sync)(void *context, bool invalidated);
    /* The GPU broker holds a live backing for pa. */
    bool (*busy)(void *context, uint64_t pa);
};

struct hv_agx_pool_window {
    struct hv_agx_pool_window_ops ops;
    uint64_t base, capacity, mapped;
    uint64_t ram_base, ram_bytes;
    /* PA of every window page (capacity / page entries). */
    uint64_t *pages;
    /* One bit per guest RAM page (ram_bytes / page bits): page in the window. */
    uint64_t *present;
    uint64_t source_ipa, source_bytes, window_offset;
    uint32_t result;
    bool available;
    /* MAP_AT left holes: MAP_APPEND is refused until UNMAP_ALL. */
    bool sparse;
    /* M2-f6: system pages of the GPU broker's backing table (REG_BROKER_PAGES);
     * 0 while unpublished. */
    uint64_t broker_pages;
};

/* Storage sizes for a window of capacity bytes over ram_bytes of RAM. */
uint64_t hv_agx_pool_window_pages_bytes(uint64_t capacity);
uint64_t hv_agx_pool_window_present_bytes(uint64_t ram_bytes);

/* Zeroed pages/present storage of the sizes above. False leaves the window
 * unavailable (every command then answers State). */
bool hv_agx_pool_window_init(struct hv_agx_pool_window *window, uint64_t base,
                             uint64_t capacity, uint64_t ram_base,
                             uint64_t ram_bytes, uint64_t *pages,
                             uint64_t *present,
                             const struct hv_agx_pool_window_ops *ops);

enum AGX_POOL_WINDOW_RESULT hv_agx_pool_window_map_append(
    struct hv_agx_pool_window *window, uint64_t source_ipa, uint64_t bytes);
enum AGX_POOL_WINDOW_RESULT hv_agx_pool_window_unmap_all(
    struct hv_agx_pool_window *window);
/* M2-f3: map or unmap any page-aligned range of the window. */
enum AGX_POOL_WINDOW_RESULT hv_agx_pool_window_map_at(
    struct hv_agx_pool_window *window, uint64_t window_offset,
    uint64_t source_ipa, uint64_t bytes);
enum AGX_POOL_WINDOW_RESULT hv_agx_pool_window_unmap_range(
    struct hv_agx_pool_window *window, uint64_t window_offset, uint64_t bytes);

/* Register access at offset (relative to AGX_POOL_WINDOW_OFFSET); width 2 is
 * 32-bit, 3 is 64-bit. False for an access outside the ABI. */
bool hv_agx_pool_window_mmio(struct hv_agx_pool_window *window, uint64_t offset,
                             uint64_t *value, bool write, unsigned width);

#endif
