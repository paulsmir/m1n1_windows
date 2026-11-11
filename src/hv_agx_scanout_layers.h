/* SPDX-License-Identifier: MIT */

#ifndef HV_AGX_SCANOUT_LAYERS_H
#define HV_AGX_SCANOUT_LAYERS_H

/* S2 (scanout broker ABI V3): the pure translation of a validated
 * COMMIT_LAYERS request into the DCP two-slot swap input.  Header-only so the
 * host tests run exactly the code the hypervisor glue runs. */

#include "dcp_iomfb_present.h"
#include "hv_agx_scanout_broker.h"

/* surface_id per DCP slot: slot 0 keeps the one-slot builder's id (3), slot 1
 * the id the two-slot builder tests and the H1 diagnostic use (4). */
#define HV_AGX_SCANOUT_DCP_SURFACE_ID_SLOT0 3u
#define HV_AGX_SCANOUT_DCP_SURFACE_ID_SLOT1 4u

static inline bool hv_agx_scanout_request_to_dcp_layers(
    const struct hv_agx_scanout_request *request, uint64_t pool_iova,
    struct dcp_iomfb_present_layer layers[DCP_IOMFB_J313_LAYERS],
    uint32_t *remove_mask)
{
    static const uint32_t surface_ids[DCP_IOMFB_J313_LAYERS] = {
        HV_AGX_SCANOUT_DCP_SURFACE_ID_SLOT0,
        HV_AGX_SCANOUT_DCP_SURFACE_ID_SLOT1,
    };

    if (!request || !layers || !remove_mask || !pool_iova ||
        request->Command != HV_AGX_SCANOUT_CMD_COMMIT_LAYERS ||
        request->LayerCount == 0 ||
        request->LayerCount > DCP_IOMFB_J313_LAYERS ||
        (request->RemoveSlot1 && request->LayerCount != 1))
        return false;
    for (uint32_t i = 0; i < DCP_IOMFB_J313_LAYERS; i++) {
        const struct hv_agx_scanout_layer *in = &request->Layers[i];
        struct dcp_iomfb_present_layer *out = &layers[i];

        *out = (struct dcp_iomfb_present_layer){0};
        if (i >= request->LayerCount)
            continue; /* iova 0: slot disabled by this swap */
        if (pool_iova > UINT64_MAX - in->SurfaceOffset ||
            (in->Format != HV_AGX_SCANOUT_FORMAT_BGRA8888 &&
             in->Format != HV_AGX_SCANOUT_FORMAT_RGBA8888))
            return false;
        out->iova = pool_iova + in->SurfaceOffset;
        out->width = in->Width;
        out->height = in->Height;
        out->stride = in->Stride;
        out->dcp_format = in->Format == HV_AGX_SCANOUT_FORMAT_RGBA8888
                              ? DCP_IOMFB_FORMAT_RGBA
                              : DCP_IOMFB_FORMAT_BGRA;
        out->source = (struct dcp_iomfb_present_rect){
            in->SrcX, in->SrcY, in->SrcWidth, in->SrcHeight};
        out->destination = (struct dcp_iomfb_present_rect){
            in->DstX, in->DstY, in->DstWidth, in->DstHeight};
        out->surface_id = surface_ids[i];
        out->premultiplied =
            (in->Flags & HV_AGX_SCANOUT_LAYER_FLAG_PREMULTIPLIED) != 0;
    }
    *remove_mask = request->RemoveSlot1 ? (1u << 1) : 0u;
    return true;
}

#endif
