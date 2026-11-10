/* SPDX-License-Identifier: MIT */
#ifndef DCP_IOMFB_PRESENT_H
#define DCP_IOMFB_PRESENT_H
#ifdef DCP_IOMFB_PRESENT_HOST_TEST
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#else
#include "types.h"
#endif
#define DCP_IOMFB_V13_5_SWAP_START_SIZE 0x18u
#define DCP_IOMFB_V13_5_SWAP_SUBMIT_INPUT_SIZE 0x1884u
#define DCP_IOMFB_V13_5_SWAP_SUBMIT_OUTPUT_SIZE 0x0cu
struct dcp_iomfb_present_request {
    uint8_t bytes[DCP_IOMFB_V13_5_SWAP_SUBMIT_INPUT_SIZE];
};
bool dcp_iomfb_present_build_start_v13_5(void *input, size_t input_size);
/* D3D-FS1: DCP surface fourccs (Asahi iomfb_plane.h DCP_FORMAT_BGRA 'ARGB',
 * DCP_FORMAT_RGBA 'ABGR'). */
#define DCP_IOMFB_FORMAT_BGRA 0x42475241u
#define DCP_IOMFB_FORMAT_RGBA 0x52474241u
/* rc106: the J313 panel the swap destination must lie inside. */
#define DCP_IOMFB_J313_PANEL_WIDTH 2560u
#define DCP_IOMFB_J313_PANEL_HEIGHT 1600u
struct dcp_iomfb_present_rect {
    uint32_t x, y, width, height;
};
/* destination: the panel rectangle DCP scales the width x height source into
 * (swap destination[0]); NULL means {0, 0, width, height}.  A destination
 * that is not the full panel sets a black background so the bars are black.
 * Refused when empty or outside the panel. */
bool dcp_iomfb_present_build_v13_5(struct dcp_iomfb_present_request *request,
                                   uint64_t surface_iova, uint32_t width,
                                   uint32_t height, uint32_t stride,
                                   uint32_t dcp_format,
                                   bool clear_boot_surfaces,
                                   const struct dcp_iomfb_present_rect *destination);
/* R13 diagnostic: two DCP surface slots on J313.  Slot count and z-order
 * from Asahi t8103.dtsi:662 (apple,iomfb-surfaces = <1 1 0 0>) and
 * apple_drv.c:294-305 (immutable zpos = slot order: slot 1 above slot 0).
 * Scaling limits from plane.c:92-95 (source/destination ratio checked
 * against 1/2 and 4); the builder admits only the conservative intersection
 * with the comment at plane.c:81-82 (at most 2x up and 2x down).  Minimum
 * blended destination 32x32 (plane.c:102-111). */
#define DCP_IOMFB_J313_LAYERS 2u
#define DCP_IOMFB_MIN_LAYER_EXTENT 32u
struct dcp_iomfb_present_layer {
    uint64_t iova;          /* 0: this slot is disabled by the swap */
    uint32_t width, height, stride, dcp_format;
    struct dcp_iomfb_present_rect source;      /* inside the surface */
    struct dcp_iomfb_present_rect destination; /* inside the panel */
    uint32_t surface_id;    /* nonzero, distinct per enabled slot */
    bool premultiplied;     /* dcp_surface.is_premultiplied */
};
/* layers[i] is DCP slot i.  Slot 0 must be enabled.  Every slot's enable
 * bit is set, so a disabled slot 1 is explicitly removed (Asahi
 * iomfb_template.c:1336-1360: enable bit set, surface left null). */
bool dcp_iomfb_present_build_layers_v13_5(
    struct dcp_iomfb_present_request *request,
    const struct dcp_iomfb_present_layer layers[DCP_IOMFB_J313_LAYERS],
    bool clear_boot_surfaces);
bool dcp_iomfb_present_set_swap_id_v13_5(
    struct dcp_iomfb_present_request *request, uint32_t swap_id);
bool dcp_iomfb_present_parse_start_v13_5(
    const void *response, size_t response_size, uint32_t *swap_id);
bool dcp_iomfb_present_parse_submit_v13_5(
    const void *response, size_t response_size);
#endif
