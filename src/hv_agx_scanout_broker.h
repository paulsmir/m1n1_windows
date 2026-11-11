/* SPDX-License-Identifier: MIT */

#ifndef HV_AGX_SCANOUT_BROKER_H
#define HV_AGX_SCANOUT_BROKER_H

#include <stdbool.h>
#include <stdint.h>

#define HV_AGX_SCANOUT_MMIO_OFFSET UINT64_C(0x400)
#define HV_AGX_SCANOUT_MMIO_SIZE UINT64_C(0xa8)
#define HV_AGX_SCANOUT_MAGIC UINT32_C(0x53584741) /* "AGXS" */
#define HV_AGX_SCANOUT_ABI_VERSION_V1 1u
#define HV_AGX_SCANOUT_ABI_VERSION_V2 2u
#define HV_AGX_SCANOUT_ABI_VERSION HV_AGX_SCANOUT_ABI_VERSION_V1

#define HV_AGX_SCANOUT_CAP_FIXED_J313 (1u << 0)
#define HV_AGX_SCANOUT_CAP_BGRA8888 (1u << 1)
/* D3D-FS1: RGBA8888 surfaces scan out as DCP RGBA */
#define HV_AGX_SCANOUT_CAP_RGBA8888 (1u << 7)
#define HV_AGX_SCANOUT_CAP_REGISTERED_POOL (1u << 2)
#define HV_AGX_SCANOUT_CAP_APPLIED_RECEIPT (1u << 3)
#define HV_AGX_SCANOUT_CAP_REPEATED_PRESENT (1u << 4)
#define HV_AGX_SCANOUT_CAP_LATCHED_RECEIPT (1u << 5)
#define HV_AGX_SCANOUT_CAP_LATCHED_IRQ (1u << 6)
/* rc106: PRESENT may carry a source smaller than the panel plus the panel
 * rectangle DCP scales it into (REG_DEST_ORIGIN / REG_DEST_SIZE).  Without
 * this bit the surface must be the exact native J313 geometry. */
#define HV_AGX_SCANOUT_CAP_SCALED_SOURCE (1u << 8)
#define HV_AGX_SCANOUT_V2_LATCH_CAPABILITIES                                  \
    (HV_AGX_SCANOUT_CAP_REPEATED_PRESENT |                                   \
     HV_AGX_SCANOUT_CAP_LATCHED_RECEIPT | HV_AGX_SCANOUT_CAP_LATCHED_IRQ)
/*
 * ABI V3 (S2, two display layers).  REG_ABI_VERSION stays V2 (the Windows
 * client admits only V1/V2): V3 is this optional capability.  It is
 * advertised only by a broker built with AGX_SCANOUT_TWO_LAYERS on top of a
 * V2 broker with the latch capabilities, and it is usable only after the
 * owner wrote it to REG_NEGOTIATED while UNREGISTERED.  Without the bit every
 * V3 offset (>= 0xa8) refuses access exactly as before V3 existed.
 */
#define HV_AGX_SCANOUT_CAP_TWO_LAYERS (1u << 9)

#define HV_AGX_SCANOUT_J313_WIDTH 2560u
#define HV_AGX_SCANOUT_J313_HEIGHT 1600u
#define HV_AGX_SCANOUT_J313_STRIDE 10240u
#define HV_AGX_SCANOUT_J313_SURFACE_SIZE UINT64_C(0xfa0000)
/* M2-f2c: the pool is segment 2 of the receipt-sized local reserve; this is
 * its J313 maximum and the default until the reserve sets the exact size. */
#define HV_AGX_SCANOUT_J313_POOL_SIZE UINT64_C(0x3b800000)
#define HV_AGX_SCANOUT_ALIGNMENT UINT64_C(0x10000)
#define HV_AGX_SCANOUT_FORMAT_BGRA8888 1u
#define HV_AGX_SCANOUT_FORMAT_RGBA8888 2u

/* V3 window: the V1/V2 window plus the V3 registers (0x400..0x540 of the
 * AGX broker page; the next user of the page starts at 0x600). */
#define HV_AGX_SCANOUT_V3_MMIO_SIZE UINT64_C(0x140)
#define HV_AGX_SCANOUT_V3_LAYERS 2u
#define HV_AGX_SCANOUT_V3_LAYER_RECORD_SIZE 0x40u
/* DCP limits the V3 broker enforces (dcp_iomfb_present.h, Asahi plane.c):
 * destination >= 32x32 on the panel, scale within 2x up and 2x down. */
#define HV_AGX_SCANOUT_V3_MIN_EXTENT 32u
#define HV_AGX_SCANOUT_V3_MAX_SCALE 2u
/* Layer record Flags: bit 0 = the surface holds premultiplied alpha (DCP
 * is_premultiplied).  Every other bit is reserved and must be zero. */
#define HV_AGX_SCANOUT_LAYER_FLAG_PREMULTIPLIED (1u << 0)
#define HV_AGX_SCANOUT_LAYER_FLAGS_VALID HV_AGX_SCANOUT_LAYER_FLAG_PREMULTIPLIED
/* REG_ACTIVE_LAYERS: DCP slots that may show a guest surface. */
#define HV_AGX_SCANOUT_ACTIVE_SLOT0 (1u << 0)
#define HV_AGX_SCANOUT_ACTIVE_SLOT1 (1u << 1)

#define HV_AGX_SCANOUT_IRQ_LATCHED (1u << 0)
#define HV_AGX_SCANOUT_IRQ_ERROR (1u << 1)
#define HV_AGX_SCANOUT_IRQ_MASK \
    (HV_AGX_SCANOUT_IRQ_LATCHED | HV_AGX_SCANOUT_IRQ_ERROR)

enum hv_agx_scanout_register {
    HV_AGX_SCANOUT_REG_MAGIC = 0x00,
    HV_AGX_SCANOUT_REG_ABI_VERSION = 0x04,
    HV_AGX_SCANOUT_REG_CAPABILITIES = 0x08,
    HV_AGX_SCANOUT_REG_STATE = 0x0c,
    HV_AGX_SCANOUT_REG_RESULT = 0x10,
    HV_AGX_SCANOUT_REG_IRQ_STATUS = 0x14,
    HV_AGX_SCANOUT_REG_IRQ_ENABLE = 0x18,
    HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE = 0x20,
    HV_AGX_SCANOUT_REG_APPLIED_SEQUENCE = 0x28,
    HV_AGX_SCANOUT_REG_LATCHED_SEQUENCE = 0x30,
    HV_AGX_SCANOUT_REG_ACTIVE_OFFSET = 0x38,
    HV_AGX_SCANOUT_REG_POOL_PA = 0x40,
    HV_AGX_SCANOUT_REG_SWAP_ID = 0x48,
    HV_AGX_SCANOUT_REG_ACCEPTED_REQUESTS = 0x50,
    HV_AGX_SCANOUT_REG_REJECTED_REQUESTS = 0x58,
    HV_AGX_SCANOUT_REG_POOL_IPA = 0x60,
    HV_AGX_SCANOUT_REG_POOL_SIZE = 0x68,
    HV_AGX_SCANOUT_REG_SURFACE_OFFSET = 0x70,
    HV_AGX_SCANOUT_REG_SURFACE_SIZE = 0x78,
    HV_AGX_SCANOUT_REG_WIDTH = 0x80,
    HV_AGX_SCANOUT_REG_HEIGHT = 0x84,
    HV_AGX_SCANOUT_REG_STRIDE = 0x88,
    HV_AGX_SCANOUT_REG_FORMAT = 0x8c,
    HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE = 0x90,
    HV_AGX_SCANOUT_REG_COMMAND = 0x98,
    /* CAP_SCALED_SOURCE only: X | Y << 16 and Width | Height << 16 of the
     * panel rectangle the source is scaled into.  Reset to the full panel. */
    HV_AGX_SCANOUT_REG_DEST_ORIGIN = 0xa0,
    HV_AGX_SCANOUT_REG_DEST_SIZE = 0xa4,
    /* ---- V3 (CAP_TWO_LAYERS advertised; otherwise not decoded) ---- */
    /* u32 rw: optional capabilities the owner uses.  A write is taken only
     * while UNREGISTERED and only with advertised optional bits
     * (CAP_TWO_LAYERS); any other write is ignored, so the guest reads it
     * back.  Cleared by a successful RELEASE (the next owner negotiates). */
    HV_AGX_SCANOUT_REG_NEGOTIATED = 0xa8,
    /* u32 rw: layers in the next COMMIT_LAYERS, 1 or 2. */
    HV_AGX_SCANOUT_REG_LAYER_COUNT = 0xac,
    /* Layer records (see enum hv_agx_scanout_layer_field): record 0 = DCP
     * slot 0 (bottom), record 1 = DCP slot 1 (top). */
    HV_AGX_SCANOUT_REG_LAYER0 = 0xb0,
    HV_AGX_SCANOUT_REG_LAYER1 = 0xf0,
    /* u32 ro: HV_AGX_SCANOUT_ACTIVE_SLOT* (bit 1 = slot 1 may be on screen;
     * the next one-layer commit or RELEASE carries the slot-1 removal). */
    HV_AGX_SCANOUT_REG_ACTIVE_LAYERS = 0x130,
    /* u64 ro: surface offset of slot 1 while ACTIVE_SLOT1 was set by an
     * applied two-layer commit; 0 otherwise. */
    HV_AGX_SCANOUT_REG_ACTIVE_OFFSET_1 = 0x138,
};
/* Field offsets inside a V3 layer record (little-endian). */
enum hv_agx_scanout_layer_field {
    HV_AGX_SCANOUT_LAYER_SURFACE_OFFSET = 0x00, /* u64, as REG_SURFACE_OFFSET */
    HV_AGX_SCANOUT_LAYER_SURFACE_SIZE = 0x08,   /* u64, = Stride * Height */
    HV_AGX_SCANOUT_LAYER_WIDTH = 0x10,          /* u32 */
    HV_AGX_SCANOUT_LAYER_HEIGHT = 0x14,         /* u32 */
    HV_AGX_SCANOUT_LAYER_STRIDE = 0x18,         /* u32, multiple of 64 */
    HV_AGX_SCANOUT_LAYER_FORMAT = 0x1c,         /* u32, HV_AGX_SCANOUT_FORMAT_* */
    HV_AGX_SCANOUT_LAYER_SRC_ORIGIN = 0x20,     /* u32 X | Y << 16 */
    HV_AGX_SCANOUT_LAYER_SRC_SIZE = 0x24,       /* u32 W | H << 16 */
    HV_AGX_SCANOUT_LAYER_DST_ORIGIN = 0x28,     /* u32 X | Y << 16 (panel) */
    HV_AGX_SCANOUT_LAYER_DST_SIZE = 0x2c,       /* u32 W | H << 16 (panel) */
    HV_AGX_SCANOUT_LAYER_FLAGS = 0x30,          /* u32 HV_AGX_SCANOUT_LAYER_FLAG_* */
    HV_AGX_SCANOUT_LAYER_RESERVED0 = 0x34,      /* u32, must be zero */
    HV_AGX_SCANOUT_LAYER_RESERVED1 = 0x38,      /* u64, must be zero */
};
#define HV_AGX_SCANOUT_DEST_PACK(low, high) \
    (((uint32_t)(low) & 0xffffu) | ((uint32_t)(high) << 16))
#define HV_AGX_SCANOUT_DEST_LOW(value) ((uint32_t)(value) & 0xffffu)
#define HV_AGX_SCANOUT_DEST_HIGH(value) ((uint32_t)(value) >> 16)
/* The destination a request carries without CAP_SCALED_SOURCE. */
#define HV_AGX_SCANOUT_J313_DEST_SIZE \
    HV_AGX_SCANOUT_DEST_PACK(HV_AGX_SCANOUT_J313_WIDTH, HV_AGX_SCANOUT_J313_HEIGHT)
enum hv_agx_scanout_command {
    HV_AGX_SCANOUT_CMD_QUERY = 0,
    HV_AGX_SCANOUT_CMD_REGISTER_POOL = 1,
    HV_AGX_SCANOUT_CMD_PRESENT = 2,
    HV_AGX_SCANOUT_CMD_RELEASE = 3,
    /* V3: one DCP swap carrying LAYER_COUNT layer records.  Completion is
     * the V2 receipt of the commit (RECEIPT/APPLIED/LATCHED_SEQUENCE,
     * SWAP_ID, IRQ_LATCHED) once for the whole commit; ACTIVE_OFFSET is
     * record 0's surface, ACTIVE_OFFSET_1 record 1's. */
    HV_AGX_SCANOUT_CMD_COMMIT_LAYERS = 4,
};

enum hv_agx_scanout_state {
    HV_AGX_SCANOUT_UNREGISTERED = 0,
    HV_AGX_SCANOUT_READY,
    HV_AGX_SCANOUT_PENDING,
    HV_AGX_SCANOUT_ACTIVE,
    HV_AGX_SCANOUT_QUIESCING,
    HV_AGX_SCANOUT_FAILED,
};

enum hv_agx_scanout_result {
    HV_AGX_SCANOUT_RESULT_OK = 0,
    HV_AGX_SCANOUT_RESULT_INVALID_COMMAND,
    HV_AGX_SCANOUT_RESULT_STALE_SEQUENCE,
    HV_AGX_SCANOUT_RESULT_BUSY,
    HV_AGX_SCANOUT_RESULT_INVALID_POOL,
    HV_AGX_SCANOUT_RESULT_UNMAPPED,
    HV_AGX_SCANOUT_RESULT_NOT_RAM,
    HV_AGX_SCANOUT_RESULT_NONCONTIGUOUS,
    HV_AGX_SCANOUT_RESULT_INVALID_SURFACE,
    HV_AGX_SCANOUT_RESULT_MAP_FAILED,
    HV_AGX_SCANOUT_RESULT_PRESENT_FAILED,
    HV_AGX_SCANOUT_RESULT_NOT_QUIESCED,
    /* V3 refusals (terminal: RECEIPT_SEQUENCE takes the commit's sequence).
     * Checked in this order; per layer record 0 first. */
    HV_AGX_SCANOUT_RESULT_LAYERS_NOT_NEGOTIATED = 12, /* CAP_TWO_LAYERS not in NEGOTIATED */
    HV_AGX_SCANOUT_RESULT_LAYER_COUNT = 13,      /* LAYER_COUNT not 1 or 2 */
    HV_AGX_SCANOUT_RESULT_LAYER_RESERVED = 14,   /* reserved field or flag bit nonzero */
    HV_AGX_SCANOUT_RESULT_LAYER_FORMAT = 15,     /* not BGRA8888 / RGBA8888 */
    HV_AGX_SCANOUT_RESULT_LAYER_GEOMETRY = 16,   /* W/H 0 or above the panel, size != stride*H */
    HV_AGX_SCANOUT_RESULT_LAYER_STRIDE = 17,     /* stride < W*4 or not a multiple of 64 */
    HV_AGX_SCANOUT_RESULT_LAYER_OUTSIDE_POOL = 18, /* offset unaligned or surface past the pool */
    HV_AGX_SCANOUT_RESULT_LAYER_SOURCE_RECT = 19, /* source empty or outside its surface */
    HV_AGX_SCANOUT_RESULT_LAYER_DEST_RECT = 20,  /* outside the panel or below 32x32 */
    HV_AGX_SCANOUT_RESULT_LAYER_SCALE = 21,      /* source/destination beyond 2x either way */
    HV_AGX_SCANOUT_RESULT_LAYER_SAME_SURFACE = 22, /* both records at one surface offset */
    HV_AGX_SCANOUT_RESULT_LAYER_OVERLAP = 23,    /* the two surfaces' byte ranges overlap */
    /* A V2 PRESENT while slot 1 may be on screen: the one-slot swap would
     * leave it there.  Commit one layer (the removal) first. */
    HV_AGX_SCANOUT_RESULT_SLOT1_LATCHED = 24,
};

/* One validated V3 layer record. */
struct hv_agx_scanout_layer {
    uint64_t SurfaceOffset;
    uint64_t SurfaceSize;
    uint32_t Width;
    uint32_t Height;
    uint32_t Stride;
    uint32_t Format;
    uint32_t SrcX, SrcY, SrcWidth, SrcHeight;
    uint32_t DstX, DstY, DstWidth, DstHeight;
    uint32_t Flags;
};

/* Raw staging of one V3 record, exactly as the guest wrote it. */
struct hv_agx_scanout_layer_staging {
    uint64_t surface_offset;
    uint64_t surface_size;
    uint32_t width, height, stride, format;
    uint32_t src_origin, src_size, dst_origin, dst_size;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t reserved1;
};

struct hv_agx_scanout_request {
    uint32_t Command;
    uint32_t Width;
    uint32_t Height;
    uint32_t Stride;
    uint32_t Format;
    uint64_t Sequence;
    uint64_t PoolIpa;
    uint64_t PoolSize;
    uint64_t SurfaceOffset;
    uint64_t SurfaceSize;
    /* Panel rectangle; the full panel unless CAP_SCALED_SOURCE admitted
     * another one.  Source rectangle is always {0, 0, Width, Height}. */
    uint32_t DestX;
    uint32_t DestY;
    uint32_t DestWidth;
    uint32_t DestHeight;
    /* V3 (COMMIT_LAYERS only; zero for V1/V2 commands).  The fields above
     * then describe record 0.  RemoveSlot1: this swap (a one-layer commit
     * or a RELEASE quiesce) must remove DCP slot 1. */
    uint32_t LayerCount;
    bool RemoveSlot1;
    struct hv_agx_scanout_layer Layers[HV_AGX_SCANOUT_V3_LAYERS];
};

struct hv_agx_scanout_broker {
    uint32_t abi_version;
    uint32_t capabilities;
    enum hv_agx_scanout_state state;
    enum hv_agx_scanout_state state_before_pending;
    enum hv_agx_scanout_result result;
    uint32_t irq_status;
    uint32_t irq_enable;
    uint32_t swap_id;
    uint64_t receipt_sequence;
    uint64_t applied_sequence;
    uint64_t latched_sequence;
    uint64_t accepted_requests;
    uint64_t rejected_requests;
    uint64_t highest_sequence;
    uint64_t active_offset;
    uint64_t pool_pa;
    uint64_t pool_iova;
    uint64_t registered_pool_ipa;
    uint64_t registered_pool_size;
    /* The only pool size REGISTER_POOL accepts. */
    uint64_t pool_size;
    uint64_t staging_pool_ipa;
    uint64_t staging_pool_size;
    uint64_t staging_surface_offset;
    uint64_t staging_surface_size;
    uint32_t staging_width;
    uint32_t staging_height;
    uint32_t staging_stride;
    uint32_t staging_format;
    uint32_t staging_dest_origin;
    uint32_t staging_dest_size;
    uint64_t staging_sequence;
    struct hv_agx_scanout_request pending;
    bool pending_taken;
    bool irq_edge_taken;
    /* V3 */
    uint32_t negotiated;
    uint32_t staging_layer_count;
    struct hv_agx_scanout_layer_staging staging_layers[HV_AGX_SCANOUT_V3_LAYERS];
    /* Slot-1 state machine: true from a two-layer commit reaching DCP until
     * the D589 of the swap that removed slot 1 (or a successful RELEASE). */
    bool slot1_latched;
    uint64_t slot1_removal_sequence;
    uint64_t active_offset_1;
};

void hv_agx_scanout_broker_init(struct hv_agx_scanout_broker *broker);
/* Sets the pool size a guest must register (segment 2 of the local reserve):
 * aligned, at least one surface, at most the J313 maximum, only while no pool
 * is registered. */
bool hv_agx_scanout_broker_set_pool_size(struct hv_agx_scanout_broker *broker,
                                         uint64_t bytes);
void hv_agx_scanout_broker_init_v2(struct hv_agx_scanout_broker *broker,
                                   bool latch_source_proven);
/* V3: advertises CAP_TWO_LAYERS.  Only on a V2 broker with the latch
 * capabilities and before any pool is registered; false otherwise. */
bool hv_agx_scanout_broker_enable_two_layers(struct hv_agx_scanout_broker *broker);
bool hv_agx_scanout_broker_mmio(struct hv_agx_scanout_broker *broker, uint64_t offset,
                                uint64_t *value, bool write, unsigned width);
bool hv_agx_scanout_broker_take_pending(struct hv_agx_scanout_broker *broker,
                                        struct hv_agx_scanout_request *request);
bool hv_agx_scanout_broker_complete_register(struct hv_agx_scanout_broker *broker,
                                             uint64_t sequence,
                                             enum hv_agx_scanout_result result,
                                             uint64_t pool_pa, uint64_t pool_iova);
bool hv_agx_scanout_broker_complete_present(struct hv_agx_scanout_broker *broker,
                                            uint64_t sequence,
                                            enum hv_agx_scanout_result result,
                                            uint32_t swap_id);
bool hv_agx_scanout_broker_mark_latched(struct hv_agx_scanout_broker *broker,
                                        uint64_t sequence);
bool hv_agx_scanout_broker_fail_latch(struct hv_agx_scanout_broker *broker,
                                      uint64_t sequence);
bool hv_agx_scanout_broker_take_irq_edge(
    struct hv_agx_scanout_broker *broker);
bool hv_agx_scanout_broker_complete_release(struct hv_agx_scanout_broker *broker,
                                            uint64_t sequence,
                                            enum hv_agx_scanout_result result,
                                            bool quiesce_proven);

#endif
