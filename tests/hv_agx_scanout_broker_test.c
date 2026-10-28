#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../src/hv_agx_scanout_broker.h"

static void write32(struct hv_agx_scanout_broker *broker, uint64_t reg, uint32_t value)
{
    uint64_t wire = value;
    assert(hv_agx_scanout_broker_mmio(broker, reg, &wire, true, 2));
}

static void write64(struct hv_agx_scanout_broker *broker, uint64_t reg, uint64_t value)
{
    assert(hv_agx_scanout_broker_mmio(broker, reg, &value, true, 3));
}

static uint32_t read32(struct hv_agx_scanout_broker *broker, uint64_t reg)
{
    uint64_t value = 0;
    assert(hv_agx_scanout_broker_mmio(broker, reg, &value, false, 2));
    return (uint32_t)value;
}

static uint64_t read64(struct hv_agx_scanout_broker *broker, uint64_t reg)
{
    uint64_t value = 0;
    assert(hv_agx_scanout_broker_mmio(broker, reg, &value, false, 3));
    return value;
}

static void stage_register(struct hv_agx_scanout_broker *broker, uint64_t sequence)
{
    write64(broker, HV_AGX_SCANOUT_REG_POOL_IPA, UINT64_C(0x10000000));
    write64(broker, HV_AGX_SCANOUT_REG_POOL_SIZE, HV_AGX_SCANOUT_J313_POOL_SIZE);
    write64(broker, HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, sequence);
    write32(broker, HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_REGISTER_POOL);
}

static void stage_present_format(struct hv_agx_scanout_broker *broker, uint64_t sequence,
                                 uint64_t offset, uint32_t format);
static void stage_present(struct hv_agx_scanout_broker *broker, uint64_t sequence,
                          uint64_t offset)
{
    stage_present_format(broker, sequence, offset, HV_AGX_SCANOUT_FORMAT_BGRA8888);
}

static void stage_present_format(struct hv_agx_scanout_broker *broker, uint64_t sequence,
                                 uint64_t offset, uint32_t format)
{
    write64(broker, HV_AGX_SCANOUT_REG_SURFACE_OFFSET, offset);
    write64(broker, HV_AGX_SCANOUT_REG_SURFACE_SIZE, HV_AGX_SCANOUT_J313_SURFACE_SIZE);
    write32(broker, HV_AGX_SCANOUT_REG_WIDTH, HV_AGX_SCANOUT_J313_WIDTH);
    write32(broker, HV_AGX_SCANOUT_REG_HEIGHT, HV_AGX_SCANOUT_J313_HEIGHT);
    write32(broker, HV_AGX_SCANOUT_REG_STRIDE, HV_AGX_SCANOUT_J313_STRIDE);
    write32(broker, HV_AGX_SCANOUT_REG_FORMAT, format);
    write64(broker, HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, sequence);
    write32(broker, HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_PRESENT);
}

static void test_identity_and_exact_mmio(void)
{
    struct hv_agx_scanout_broker broker;
    uint64_t value = 0;

    memset(&broker, 0xa5, sizeof(broker));
    hv_agx_scanout_broker_init(&broker);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_MAGIC) == HV_AGX_SCANOUT_MAGIC);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_ABI_VERSION) ==
           HV_AGX_SCANOUT_ABI_VERSION);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_CAPABILITIES) ==
           (HV_AGX_SCANOUT_CAP_FIXED_J313 | HV_AGX_SCANOUT_CAP_BGRA8888 |
            HV_AGX_SCANOUT_CAP_REGISTERED_POOL | HV_AGX_SCANOUT_CAP_APPLIED_RECEIPT |
            HV_AGX_SCANOUT_CAP_RGBA8888));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_UNREGISTERED);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE) == 0);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_APPLIED_SEQUENCE) == 0);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_LATCHED_SEQUENCE) == 0);
    assert(!hv_agx_scanout_broker_mmio(&broker, HV_AGX_SCANOUT_REG_MAGIC, &value, true, 2));
    assert(!hv_agx_scanout_broker_mmio(&broker, HV_AGX_SCANOUT_REG_MAGIC, &value, false, 3));
    assert(!hv_agx_scanout_broker_mmio(&broker, 0x7c, &value, false, 2));
    /* rc106: the window ends after DEST_SIZE; the staging default is the
     * full panel and v1 does not advertise scaling. */
    assert(!(read32(&broker, HV_AGX_SCANOUT_REG_CAPABILITIES) &
             HV_AGX_SCANOUT_CAP_SCALED_SOURCE));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_DEST_ORIGIN) == 0);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_DEST_SIZE) ==
           (2560u | (1600u << 16)));
    assert(!hv_agx_scanout_broker_mmio(&broker, 0xa8, &value, false, 2));
    assert(!hv_agx_scanout_broker_mmio(&broker, 0xa8, &value, true, 2));
    assert(!hv_agx_scanout_broker_mmio(&broker, 0xa0, &value, false, 3));
}

/* rc106: PRESENT with a smaller source and a panel destination. */
static void stage_scaled_present(struct hv_agx_scanout_broker *broker,
                                 uint64_t sequence, uint64_t offset,
                                 uint32_t width, uint32_t height,
                                 uint32_t stride, uint64_t size,
                                 uint32_t dest_origin, uint32_t dest_size)
{
    write64(broker, HV_AGX_SCANOUT_REG_SURFACE_OFFSET, offset);
    write64(broker, HV_AGX_SCANOUT_REG_SURFACE_SIZE, size);
    write32(broker, HV_AGX_SCANOUT_REG_WIDTH, width);
    write32(broker, HV_AGX_SCANOUT_REG_HEIGHT, height);
    write32(broker, HV_AGX_SCANOUT_REG_STRIDE, stride);
    write32(broker, HV_AGX_SCANOUT_REG_FORMAT, HV_AGX_SCANOUT_FORMAT_BGRA8888);
    write32(broker, HV_AGX_SCANOUT_REG_DEST_ORIGIN, dest_origin);
    write32(broker, HV_AGX_SCANOUT_REG_DEST_SIZE, dest_size);
    write64(broker, HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, sequence);
    write32(broker, HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_PRESENT);
}

static void register_ready(struct hv_agx_scanout_broker *broker)
{
    struct hv_agx_scanout_request request;

    stage_register(broker, 1);
    assert(hv_agx_scanout_broker_take_pending(broker, &request));
    assert(hv_agx_scanout_broker_complete_register(
        broker, 1, HV_AGX_SCANOUT_RESULT_OK, UINT64_C(0x880000000),
        UINT64_C(0xf00000000)));
    assert(read32(broker, HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_READY);
}

static void test_v1_stays_native_only_and_ignores_destination(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_request request;

    hv_agx_scanout_broker_init(&broker);
    register_ready(&broker);
    /* A non-native source is still refused without the cap. */
    stage_scaled_present(&broker, 2, 0, 1920, 1080, 7680, 7680u * 1080u,
                         0, 2560u | (1440u << 16));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) ==
           HV_AGX_SCANOUT_RESULT_INVALID_SURFACE);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_READY);
    /* A native present carries the full panel whatever was staged. */
    stage_scaled_present(&broker, 3, 0, 2560, 1600, 10240,
                         HV_AGX_SCANOUT_J313_SURFACE_SIZE,
                         80u | (80u << 16), 1280u | (720u << 16));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_OK);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(request.Width == 2560 && request.Height == 1600 &&
           request.Stride == 10240);
    assert(request.DestX == 0 && request.DestY == 0);
    assert(request.DestWidth == 2560 && request.DestHeight == 1600);
}

static void test_v2_scaled_source_destination_reaches_the_request(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_request request;

    hv_agx_scanout_broker_init_v2(&broker, true);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_CAPABILITIES) &
           HV_AGX_SCANOUT_CAP_SCALED_SOURCE);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_DEST_ORIGIN) == 0);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_DEST_SIZE) ==
           (2560u | (1600u << 16)));
    register_ready(&broker);

    /* Native geometry with the default destination: the request is the
     * same as before rc106. */
    stage_present(&broker, 2, 0x10000);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_OK);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(request.Width == 2560 && request.Height == 1600 &&
           request.Stride == 10240 &&
           request.SurfaceSize == HV_AGX_SCANOUT_J313_SURFACE_SIZE);
    assert(request.DestX == 0 && request.DestY == 0);
    assert(request.DestWidth == 2560 && request.DestHeight == 1600);
    assert(hv_agx_scanout_broker_complete_present(&broker, 2,
                                                    HV_AGX_SCANOUT_RESULT_OK, 7));
    assert(hv_agx_scanout_broker_mark_latched(&broker, 2));

    /* 1920x1080 aspect-ratio-centered-max: 2560x1440 at (0, 80). */
    stage_scaled_present(&broker, 3, 0x20000, 1920, 1080, 7680, 7680u * 1080u,
                         0u | (80u << 16), 2560u | (1440u << 16));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_OK);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_PENDING);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(request.Command == HV_AGX_SCANOUT_CMD_PRESENT);
    assert(request.SurfaceOffset == 0x20000);
    assert(request.SurfaceSize == 7680u * 1080u);
    assert(request.Width == 1920 && request.Height == 1080 &&
           request.Stride == 7680);
    assert(request.DestX == 0 && request.DestY == 80);
    assert(request.DestWidth == 2560 && request.DestHeight == 1440);
    assert(hv_agx_scanout_broker_complete_present(&broker, 3,
                                                    HV_AGX_SCANOUT_RESULT_OK, 8));
    assert(hv_agx_scanout_broker_mark_latched(&broker, 3));

    /* Centered 1280x720 at (640, 440); a padded stride is fine. */
    stage_scaled_present(&broker, 4, 0x30000, 1280, 720, 5184, 5184u * 720u,
                         640u | (440u << 16), 1280u | (720u << 16));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_OK);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(request.Stride == 5184 && request.DestX == 640 && request.DestY == 440);
    assert(hv_agx_scanout_broker_complete_present(&broker, 4,
                                                    HV_AGX_SCANOUT_RESULT_OK, 9));
    assert(hv_agx_scanout_broker_mark_latched(&broker, 4));

    /* The 800x600 corner: 2133x1600 at (213, 0) is inside the panel. */
    stage_scaled_present(&broker, 5, 0x40000, 800, 600, 3200, 3200u * 600u,
                         213u, 2133u | (1600u << 16));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_OK);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_present(&broker, 5,
                                                    HV_AGX_SCANOUT_RESULT_OK, 10));
    assert(hv_agx_scanout_broker_mark_latched(&broker, 5));
}

static void test_v2_scaled_source_refusals(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_request request;
    uint64_t seq = 2;
    static const struct {
        uint32_t width, height, stride;
        uint64_t size;
        uint32_t dest_origin, dest_size;
    } bad[] = {
        /* destination leaves the panel */
        {1920, 1080, 7680, 7680u * 1080u, 0u | (161u << 16), 2560u | (1440u << 16)},
        {1920, 1080, 7680, 7680u * 1080u, 1u, 2560u | (1440u << 16)},
        {2560, 1600, 10240, 0xfa0000u, 0u | (1u << 16), 2560u | (1600u << 16)},
        {1920, 1080, 7680, 7680u * 1080u, 0u, 2561u | (1440u << 16)},
        {1920, 1080, 7680, 7680u * 1080u, 0u, 2560u | (1601u << 16)},
        /* empty destination */
        {1920, 1080, 7680, 7680u * 1080u, 0u, 0u | (1440u << 16)},
        {1920, 1080, 7680, 7680u * 1080u, 0u, 2560u},
        /* source larger than the panel */
        {2561, 1080, 10304, 10304u * 1080u, 0u, 2560u | (1600u << 16)},
        {1920, 1601, 7680, 7680u * 1601u, 0u, 2560u | (1600u << 16)},
        {0, 1080, 7680, 0, 0u, 2560u | (1600u << 16)},
        {1920, 0, 7680, 0, 0u, 2560u | (1600u << 16)},
        /* stride rules: short, not a multiple of 64 (1366x768) */
        {1920, 1080, 7616, 7616u * 1080u, 0u, 2560u | (1600u << 16)},
        {1366, 768, 5464, 5464u * 768u, 0u, 2560u | (1600u << 16)},
        /* size must equal stride * height */
        {1920, 1080, 7680, 7680u * 1080u + 64u, 0u, 2560u | (1600u << 16)},
        {1920, 1080, 7680, 0xfa0000u, 0u, 2560u | (1600u << 16)},
    };

    hv_agx_scanout_broker_init_v2(&broker, true);
    register_ready(&broker);
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        stage_scaled_present(&broker, seq, 0, bad[i].width, bad[i].height,
                             bad[i].stride, bad[i].size, bad[i].dest_origin,
                             bad[i].dest_size);
        assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) ==
               HV_AGX_SCANOUT_RESULT_INVALID_SURFACE);
        assert(read64(&broker, HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE) == seq);
        assert(read32(&broker, HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_READY);
        assert(!hv_agx_scanout_broker_take_pending(&broker, &request));
        seq++;
    }
    /* A surface that runs past the pool is still refused. */
    stage_scaled_present(&broker, seq, HV_AGX_SCANOUT_J313_POOL_SIZE - 0x10000,
                         1920, 1080, 7680, 7680u * 1080u, 0u | (80u << 16),
                         2560u | (1440u << 16));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) ==
           HV_AGX_SCANOUT_RESULT_INVALID_SURFACE);
    seq++;
    /* The same geometry at a valid offset is accepted. */
    stage_scaled_present(&broker, seq, 0, 1920, 1080, 7680, 7680u * 1080u,
                         0u | (80u << 16), 2560u | (1440u << 16));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_OK);
}

static void test_register_snapshot_and_completion(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_request request;

    hv_agx_scanout_broker_init(&broker);
    stage_register(&broker, 1);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_PENDING);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE) == 0);
    write64(&broker, HV_AGX_SCANOUT_REG_POOL_IPA, UINT64_C(0x20000000));
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(request.Command == HV_AGX_SCANOUT_CMD_REGISTER_POOL);
    assert(request.Sequence == 1);
    assert(request.PoolIpa == UINT64_C(0x10000000));
    assert(request.PoolSize == HV_AGX_SCANOUT_J313_POOL_SIZE);
    assert(!hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_register(
        &broker, 1, HV_AGX_SCANOUT_RESULT_OK, UINT64_C(0x880000000),
        UINT64_C(0xf00000000)));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_READY);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE) == 1);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_APPLIED_SEQUENCE) == 1);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_POOL_PA) == UINT64_C(0x880000000));
}

static void test_sequence_and_busy_rejection(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_request request;

    hv_agx_scanout_broker_init(&broker);
    stage_register(&broker, 1);
    stage_register(&broker, 2);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_BUSY);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_REJECTED_REQUESTS) == 1);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE) == 0);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(request.Sequence == 1);
    assert(hv_agx_scanout_broker_complete_register(
        &broker, 1, HV_AGX_SCANOUT_RESULT_OK, UINT64_C(0x880000000),
        UINT64_C(0xf00000000)));
    stage_present(&broker, 2, 1);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE) == 2);
    stage_register(&broker, 1);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) ==
           HV_AGX_SCANOUT_RESULT_STALE_SEQUENCE);
    write64(&broker, HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, 0);
    write32(&broker, HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_QUERY);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) ==
           HV_AGX_SCANOUT_RESULT_STALE_SEQUENCE);
}

static void test_surface_validation_and_latch_separation(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_request request;
    uint64_t highest = HV_AGX_SCANOUT_J313_POOL_SIZE - HV_AGX_SCANOUT_J313_SURFACE_SIZE;

    hv_agx_scanout_broker_init(&broker);
    stage_register(&broker, 1);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_register(
        &broker, 1, HV_AGX_SCANOUT_RESULT_OK, UINT64_C(0x880000000),
        UINT64_C(0xf00000000)));

    stage_present(&broker, 2, highest & ~UINT64_C(0xffff));
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(request.Command == HV_AGX_SCANOUT_CMD_PRESENT);
    assert(request.SurfaceOffset == (highest & ~UINT64_C(0xffff)));
    assert(hv_agx_scanout_broker_complete_present(&broker, 2,
                                                    HV_AGX_SCANOUT_RESULT_OK, 17));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_ACTIVE);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_APPLIED_SEQUENCE) == 2);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_LATCHED_SEQUENCE) == 0);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_ACTIVE_OFFSET) ==
           (highest & ~UINT64_C(0xffff)));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_SWAP_ID) == 17);
    assert(!hv_agx_scanout_broker_mark_latched(&broker, 2));
    assert(read64(&broker, HV_AGX_SCANOUT_REG_LATCHED_SEQUENCE) == 0);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_IRQ_STATUS) == 0);

    stage_present(&broker, 3, 0);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_BUSY);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE) == 2);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_APPLIED_SEQUENCE) == 2);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_SWAP_ID) == 17);

    stage_present(&broker, 4, 1);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) ==
           HV_AGX_SCANOUT_RESULT_INVALID_SURFACE);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE) == 4);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_ACTIVE_OFFSET) ==
           (highest & ~UINT64_C(0xffff)));
}

static void test_v2_requires_exact_latch_before_repeated_present(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_request request;

    hv_agx_scanout_broker_init_v2(&broker, true);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_ABI_VERSION) == 2);
    assert((read32(&broker, HV_AGX_SCANOUT_REG_CAPABILITIES) &
            (HV_AGX_SCANOUT_CAP_REPEATED_PRESENT |
             HV_AGX_SCANOUT_CAP_LATCHED_RECEIPT |
             HV_AGX_SCANOUT_CAP_LATCHED_IRQ)) ==
           (HV_AGX_SCANOUT_CAP_REPEATED_PRESENT |
            HV_AGX_SCANOUT_CAP_LATCHED_RECEIPT |
            HV_AGX_SCANOUT_CAP_LATCHED_IRQ));

    stage_register(&broker, 1);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_register(
        &broker, 1, HV_AGX_SCANOUT_RESULT_OK, UINT64_C(0x880000000),
        UINT64_C(0xf00000000)));
    stage_present(&broker, 2, 0);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_present(
        &broker, 2, HV_AGX_SCANOUT_RESULT_OK, 31));
    assert(read64(&broker, HV_AGX_SCANOUT_REG_APPLIED_SEQUENCE) == 2);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_LATCHED_SEQUENCE) == 0);

    stage_present(&broker, 3, HV_AGX_SCANOUT_ALIGNMENT);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) ==
           HV_AGX_SCANOUT_RESULT_BUSY);
    assert(!hv_agx_scanout_broker_mark_latched(&broker, 1));
    assert(hv_agx_scanout_broker_mark_latched(&broker, 2));
    assert(read64(&broker, HV_AGX_SCANOUT_REG_LATCHED_SEQUENCE) == 2);
    assert((read32(&broker, HV_AGX_SCANOUT_REG_IRQ_STATUS) &
            HV_AGX_SCANOUT_IRQ_LATCHED) != 0);

    write32(&broker, HV_AGX_SCANOUT_REG_IRQ_STATUS,
            HV_AGX_SCANOUT_IRQ_LATCHED);
    stage_present(&broker, 4, HV_AGX_SCANOUT_ALIGNMENT);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_present(
        &broker, 4, HV_AGX_SCANOUT_RESULT_OK, 32));
    assert(hv_agx_scanout_broker_mark_latched(&broker, 4));
    assert(read64(&broker, HV_AGX_SCANOUT_REG_APPLIED_SEQUENCE) == 4);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_LATCHED_SEQUENCE) == 4);
}

static void test_v2_without_latch_source_stays_fail_closed(void)
{
    struct hv_agx_scanout_broker broker;

    hv_agx_scanout_broker_init_v2(&broker, false);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_ABI_VERSION) == 2);
    assert((read32(&broker, HV_AGX_SCANOUT_REG_CAPABILITIES) &
            (HV_AGX_SCANOUT_CAP_REPEATED_PRESENT |
             HV_AGX_SCANOUT_CAP_LATCHED_RECEIPT |
             HV_AGX_SCANOUT_CAP_LATCHED_IRQ)) == 0);
    assert(!hv_agx_scanout_broker_mark_latched(&broker, 1));
}

static void test_v2_irq_injects_only_once_per_enabled_pending_edge(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_request request;

    hv_agx_scanout_broker_init_v2(&broker, true);
    stage_register(&broker, 1);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_register(
        &broker, 1, HV_AGX_SCANOUT_RESULT_OK, UINT64_C(0x880000000),
        UINT64_C(0xf00000000)));
    write32(&broker, HV_AGX_SCANOUT_REG_IRQ_ENABLE,
            HV_AGX_SCANOUT_IRQ_LATCHED | HV_AGX_SCANOUT_IRQ_ERROR);
    stage_present(&broker, 2, 0);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_present(
        &broker, 2, HV_AGX_SCANOUT_RESULT_OK, 31));
    assert(hv_agx_scanout_broker_mark_latched(&broker, 2));
    assert(hv_agx_scanout_broker_take_irq_edge(&broker));
    assert(!hv_agx_scanout_broker_take_irq_edge(&broker));
    write32(&broker, HV_AGX_SCANOUT_REG_IRQ_STATUS,
            HV_AGX_SCANOUT_IRQ_LATCHED);
    assert(!hv_agx_scanout_broker_take_irq_edge(&broker));

    stage_present(&broker, 3, HV_AGX_SCANOUT_ALIGNMENT);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_present(
        &broker, 3, HV_AGX_SCANOUT_RESULT_OK, 32));
    assert(hv_agx_scanout_broker_mark_latched(&broker, 3));
    assert(hv_agx_scanout_broker_take_irq_edge(&broker));
}

static void test_failed_present_preserves_active_and_release_fails_closed(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_request request;

    hv_agx_scanout_broker_init(&broker);
    stage_register(&broker, 1);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_register(
        &broker, 1, HV_AGX_SCANOUT_RESULT_OK, UINT64_C(0x880000000),
        UINT64_C(0xf00000000)));
    stage_present(&broker, 2, 0);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_present(
        &broker, 2, HV_AGX_SCANOUT_RESULT_PRESENT_FAILED, 0));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_READY);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_ACTIVE_OFFSET) == 0);
    write64(&broker, HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, 3);
    write32(&broker, HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_RELEASE);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_release(
        &broker, 3, HV_AGX_SCANOUT_RESULT_NOT_QUIESCED, false));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_READY);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_POOL_PA) == UINT64_C(0x880000000));
}

static void test_register_rejects_invalid_mapping(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_request request;

    hv_agx_scanout_broker_init(&broker);
    stage_register(&broker, 1);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_register(
        &broker, 1, HV_AGX_SCANOUT_RESULT_OK, UINT64_C(0x880000000), 1));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) ==
           HV_AGX_SCANOUT_RESULT_MAP_FAILED);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_UNREGISTERED);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_APPLIED_SEQUENCE) == 0);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_POOL_PA) == 0);
}

/* M2-f2c: the hypervisor sets the pool size from the receipt-sized reserve;
 * only that size registers. */
static void test_pool_size_follows_reserve(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_request request;

    hv_agx_scanout_broker_init(&broker);
    assert(broker.pool_size == HV_AGX_SCANOUT_J313_POOL_SIZE);
    assert(!hv_agx_scanout_broker_set_pool_size(&broker,
                                                HV_AGX_SCANOUT_J313_POOL_SIZE + 0x10000));
    assert(!hv_agx_scanout_broker_set_pool_size(&broker, UINT64_C(0x13804000)));
    assert(!hv_agx_scanout_broker_set_pool_size(&broker, UINT64_C(0x800000)));
    assert(hv_agx_scanout_broker_set_pool_size(&broker, UINT64_C(0x13800000)));
    /* A full-size registration no longer matches. */
    stage_register(&broker, 1);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_INVALID_POOL);

    hv_agx_scanout_broker_init(&broker);
    assert(hv_agx_scanout_broker_set_pool_size(&broker, UINT64_C(0x13800000)));
    write64(&broker, HV_AGX_SCANOUT_REG_POOL_IPA, UINT64_C(0x10000000));
    write64(&broker, HV_AGX_SCANOUT_REG_POOL_SIZE, UINT64_C(0x13800000));
    write64(&broker, HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, 1);
    write32(&broker, HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_REGISTER_POOL);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(request.PoolSize == UINT64_C(0x13800000));
    /* Once a registration is pending the size is fixed. */
    assert(!hv_agx_scanout_broker_set_pool_size(&broker, HV_AGX_SCANOUT_J313_POOL_SIZE));
}

static void test_active_release_failure_preserves_ownership(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_request request;

    hv_agx_scanout_broker_init(&broker);
    stage_register(&broker, 1);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_register(
        &broker, 1, HV_AGX_SCANOUT_RESULT_OK, UINT64_C(0x880000000),
        UINT64_C(0xf00000000)));
    stage_present(&broker, 2, 0);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_present(
        &broker, 2, HV_AGX_SCANOUT_RESULT_OK, 19));
    write64(&broker, HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, 3);
    write32(&broker, HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_RELEASE);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_release(
        &broker, 3, HV_AGX_SCANOUT_RESULT_NOT_QUIESCED, false));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_ACTIVE);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_POOL_PA) == UINT64_C(0x880000000));
    assert(broker.pool_iova == UINT64_C(0xf00000000));
    assert(read64(&broker, HV_AGX_SCANOUT_REG_ACTIVE_OFFSET) == 0);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_SWAP_ID) == 19);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_APPLIED_SEQUENCE) == 2);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_LATCHED_SEQUENCE) == 0);
}

static void test_release_without_quiesce_proof_fails_closed(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_request request;

    hv_agx_scanout_broker_init(&broker);
    stage_register(&broker, 1);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_register(
        &broker, 1, HV_AGX_SCANOUT_RESULT_OK, UINT64_C(0x880000000),
        UINT64_C(0xf00000000)));
    stage_present(&broker, 2, 0);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_present(
        &broker, 2, HV_AGX_SCANOUT_RESULT_OK, 23));
    write64(&broker, HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, 3);
    write32(&broker, HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_RELEASE);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_release(
        &broker, 3, HV_AGX_SCANOUT_RESULT_OK, false));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) ==
           HV_AGX_SCANOUT_RESULT_NOT_QUIESCED);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_ACTIVE);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_POOL_PA) == UINT64_C(0x880000000));
    assert(read32(&broker, HV_AGX_SCANOUT_REG_SWAP_ID) == 23);
    assert(read64(&broker, HV_AGX_SCANOUT_REG_APPLIED_SEQUENCE) == 2);
}

/* D3D-FS1: an RGBA8888 present is accepted and carries its format to the
 * consumer; any other format is an invalid surface. */
static void test_rgba_present_format_reaches_the_request(void)
{
    struct hv_agx_scanout_broker broker;
    struct hv_agx_scanout_request request;

    hv_agx_scanout_broker_init(&broker);
    stage_register(&broker, 1);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(hv_agx_scanout_broker_complete_register(
        &broker, 1, HV_AGX_SCANOUT_RESULT_OK, UINT64_C(0x880000000),
        UINT64_C(0xf00000000)));
    stage_present_format(&broker, 2, 0, HV_AGX_SCANOUT_FORMAT_RGBA8888);
    assert(hv_agx_scanout_broker_take_pending(&broker, &request));
    assert(request.Format == HV_AGX_SCANOUT_FORMAT_RGBA8888);
    assert(hv_agx_scanout_broker_complete_present(&broker, 2,
                                                    HV_AGX_SCANOUT_RESULT_OK, 5));
    stage_present_format(&broker, 3, 0, 3u);
    assert(read32(&broker, HV_AGX_SCANOUT_REG_RESULT) ==
           HV_AGX_SCANOUT_RESULT_INVALID_SURFACE);
}

int main(void)
{
    test_rgba_present_format_reaches_the_request();
    test_identity_and_exact_mmio();
    test_v1_stays_native_only_and_ignores_destination();
    test_v2_scaled_source_destination_reaches_the_request();
    test_v2_scaled_source_refusals();
    test_register_snapshot_and_completion();
    test_sequence_and_busy_rejection();
    test_surface_validation_and_latch_separation();
    test_v2_requires_exact_latch_before_repeated_present();
    test_v2_without_latch_source_stays_fail_closed();
    test_v2_irq_injects_only_once_per_enabled_pending_edge();
    test_failed_present_preserves_active_and_release_fails_closed();
    test_register_rejects_invalid_mapping();
    test_pool_size_follows_reserve();
    test_active_release_failure_preserves_ownership();
    test_release_without_quiesce_proof_fails_closed();
    return 0;
}
