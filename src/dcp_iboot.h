/* SPDX-License-Identifier: MIT */

#ifndef DCP_IBOOT_H
#define DCP_IBOOT_H

#include "dcp.h"
#include "afk_command.h"

typedef struct dcp_iboot_if dcp_iboot_if_t;

#define DCP_IB_ASYNC_BUFFER_SIZE 0x4000

enum dcp_ib_swap_async_result {
    DCP_IB_SWAP_ASYNC_FAILED = -1,
    DCP_IB_SWAP_ASYNC_PENDING = 0,
    DCP_IB_SWAP_ASYNC_APPLIED = 1,
};

enum dcp_ib_swap_async_stage {
    DCP_IB_SWAP_ASYNC_IDLE = 0,
    DCP_IB_SWAP_ASYNC_BEGIN,
    DCP_IB_SWAP_ASYNC_SET_LAYER,
    DCP_IB_SWAP_ASYNC_END,
    DCP_IB_SWAP_ASYNC_COMPLETE,
    DCP_IB_SWAP_ASYNC_ERROR,
};

enum DCPEOTF {
    EOTF_GAMMA_SDR = 1,
    EOTF_GAMMA_HDR = 2,
};

enum DCPEncoding {
    ENC_RGB = 1,
    ENC_YCBCR_444 = 3,
    ENC_YCBCR_422 = 4,
    ENC_YCBCR_420 = 5,
};

enum DCPColorimetry {
    CLR_BT601_709 = 1,
    CLR_BT2020 = 2,
    CLR_DCIP3 = 3,
};

enum DCPSurfaceFmt {
    FMT_BGRA = 1,
    FMT_RGBA = 3,
    FMT_w18p = 4,
    FMT_444v = 6,
    FMT_422v = 7,
    FMT_420v = 8,
    FMT_w30r = 9,
    FMT_w40a = 10,
};

enum DCPTransform {
    XFRM_NONE = 0,
    XFRM_XFLIP = 1,
    XFRM_YFLIP = 2,
    XFRM_ROT_90 = 3,
    XFRM_ROT_180 = 4,
    XFRM_ROT_270 = 5,
};

enum AddrFormat {
    ADDR_PLANAR = 1,
    ADDR_TILED = 2,
    ADDR_AGX = 3,
};

typedef struct {
    u32 valid;
    u32 width;
    u32 height;
    u32 fps;
    u8 pad[8];
} PACKED dcp_timing_mode_t;

typedef struct {
    u32 valid;
    u32 colorimetry;
    u32 eotf;
    u32 encoding;
    u32 bpp;
    u8 pad[4];
} PACKED dcp_color_mode_t;

typedef struct {
    u32 unk1;
    u64 addr;
    u32 tile_size;
    u32 stride;
    u32 unk2[4];
    u32 addr_format;
    u32 unk3;
} PACKED dcp_plane_t;

typedef struct {
    dcp_plane_t planes[3];
    u32 unk;
    u32 plane_cnt;
    u32 width;
    u32 height;
    u32 surface_fmt;
    u32 colorspace;
    u32 eotf;
    u8 transform;
    u8 padding[3];
} PACKED dcp_layer_t;

typedef struct {
    u32 w, h, x, y;
} PACKED dcp_rect_t;

typedef struct dcp_ib_swap_async {
    dcp_iboot_if_t *iboot;
    afk_epic_command_t command;
    enum dcp_ib_swap_async_stage stage;
    dcp_layer_t layer;
    dcp_rect_t source;
    dcp_rect_t target;
    int swap_id;
    u8 txbuf[DCP_IB_ASYNC_BUFFER_SIZE];
    u8 rxbuf[DCP_IB_ASYNC_BUFFER_SIZE];
} dcp_ib_swap_async_t;

dcp_iboot_if_t *dcp_ib_init(dcp_dev_t *dcp);
int dcp_ib_shutdown(dcp_iboot_if_t *iboot);

int dcp_ib_set_surface(dcp_iboot_if_t *iboot, dcp_layer_t *layer);
int dcp_ib_set_power(dcp_iboot_if_t *iboot, bool power);
int dcp_ib_get_hpd(dcp_iboot_if_t *iboot, int *timing_cnt, int *color_cnt);
int dcp_ib_get_timing_modes(dcp_iboot_if_t *iboot, dcp_timing_mode_t **modes);
int dcp_ib_get_color_modes(dcp_iboot_if_t *iboot, dcp_color_mode_t **modes);
int dcp_ib_set_mode(dcp_iboot_if_t *iboot, dcp_timing_mode_t *timing, dcp_color_mode_t *color);
int dcp_ib_swap_begin(dcp_iboot_if_t *iboot);
int dcp_ib_swap_set_layer(dcp_iboot_if_t *iboot, int layer_id, dcp_layer_t *layer,
                          dcp_rect_t *src_rect, dcp_rect_t *dst_rect);
int dcp_ib_swap_end(dcp_iboot_if_t *iboot);
void dcp_ib_swap_async_init(dcp_ib_swap_async_t *swap);
int dcp_ib_swap_async_begin(dcp_ib_swap_async_t *swap, dcp_iboot_if_t *iboot,
                            const dcp_layer_t *layer, const dcp_rect_t *source,
                            const dcp_rect_t *target);
enum dcp_ib_swap_async_result dcp_ib_swap_async_poll(dcp_ib_swap_async_t *swap);

#endif
