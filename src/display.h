/* SPDX-License-Identifier: MIT */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "dcp.h"
#include "dcp_iboot.h"
#include "types.h"

typedef enum _dcp_shutdown_mode {
    DCP_QUIESCED = 0,
    DCP_SLEEP_IF_EXTERNAL = 1,
    DCP_SLEEP = 2,
} dcp_shutdown_mode;

extern bool display_is_external;

int display_init(void);
int display_start_dcp(void);
int display_prepare_guest_surface(u64 base, u64 size, u32 width, u32 height, u32 stride,
                                  u32 depth);
int display_configure(const char *config);
void display_shutdown(dcp_shutdown_mode mode);
bool display_shutdown_complete(void);
const display_config_t *display_get_config(void);

/* Non-blocking fixed-panel backend used by the EL2 scanout broker. */
/* R13 diagnostic (proxy P_DISPLAY_DIAG_OVERLAY): show a solid test surface
 * in DCP slot 1 over the current slot-0 surface, observe the latch, hold,
 * then remove slot 1 again unless DISPLAY_DIAG_OVERLAY_KEEP is set. */
#define DISPLAY_DIAG_OVERLAY_PREMULTIPLIED (1u << 0)
#define DISPLAY_DIAG_OVERLAY_SPLIT_ALPHA0 (1u << 1) /* right half 0x00000000 */
#define DISPLAY_DIAG_OVERLAY_KEEP (1u << 2)
int display_diag_overlay(u32 argb, u32 x, u32 y, u32 width, u32 height,
                         u32 flags_and_hold_ms);
bool display_scanout_ready(void);
bool display_scanout_latch_source_proven(void);
bool display_scanout_reserve_iova(u64 size, u64 alignment, u64 *iova);
void display_scanout_free_iova(u64 iova, u64 size);
bool display_scanout_map(unsigned dart_index, u64 iova, u64 pa, u64 size);
void display_scanout_unmap(unsigned dart_index, u64 iova, u64 size);
/* D3D-FS1: rgba selects the RGBA8888 surface order (else BGRA8888).
 * rc106: destination is the panel rectangle the width x height source is
 * scaled into (NULL: the aspect-fit rectangle, the full panel for a native
 * source); it must be non-empty and inside the panel. */
bool display_scanout_present_begin(u64 surface_iova, u32 width, u32 height,
                                   u32 stride, bool rgba,
                                   const dcp_rect_t *destination, u64 *cookie);
int display_scanout_present_poll(u64 cookie, u32 *swap_id);
int display_scanout_latch_poll(u32 expected_swap_id);
bool display_scanout_observe_latched(u32 *swap_id, u64 *surface_iova);
bool display_scanout_quiesce_begin(u64 *cookie);
int display_scanout_quiesce_poll(u64 cookie);
/* S2 (broker ABI V3, IOMFB owner only): one swap carrying DCP slots 0 and 1
 * (dcp_iomfb_owner_present_slots); completion via display_scanout_present_poll
 * and display_scanout_latch_poll.  quiesce_slot1_begin is quiesce_begin whose
 * swap also removes slot 1; completion via display_scanout_quiesce_poll. */
bool display_scanout_present_layers_begin(
    const struct dcp_iomfb_present_layer layers[DCP_IOMFB_J313_LAYERS],
    u32 remove_mask, u64 *cookie);
bool display_scanout_quiesce_slot1_begin(u64 *cookie);

#endif
