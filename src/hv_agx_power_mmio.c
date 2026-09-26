/* SPDX-License-Identifier: MIT */

#include "../build/build_cfg.h"
#include "hv.h"
#include "adt.h"
#include "hv_agx_config_snapshot.h"
#include "hv_agx_firmware_prefix.h"
#include "hv_agx_local_reserve.h"
#include "hv_autonomous_layout.generated.h"
#include "hv_agx_retained_platform.h"
#include "../../drivers/apple-agx/shared/include/apple_agx_firmware_io.h"
#include "../../drivers/apple-agx/shared/include/apple_agx_hwdata_profile_abi.h"
#include "hv_agx_retained_mmio.h"
#include "../../drivers/apple-agx/shared/include/apple_agx_gpuva_broker_v5.h"
#include "hv_agx_abi_admission.generated.h"
#include "hv_agx_g2.generated.h"
#include "hv_agx_power_broker.h"
#include "hv_agx_scanout_broker.h"
#include "hv_agx_scanout_service.h"
#include "hv_vgic.h"
#include "display.h"
#include "memory.h"
#include "utils.h"
#include "xnuboot.h"

static struct hv_agx_power_broker broker;
static struct hv_agx_scanout_broker scanout_broker;
static struct hv_agx_scanout_service scanout_service;
static struct hv_agx_config_snapshot config_snapshot;
static bool config_snapshot_valid;
static DECLARE_SPINLOCK(broker_lock);
static DECLARE_SPINLOCK(service_lock);
static bool resources_mapped;
static struct hv_agx_local_receipt local_reserve;
static u64 firmware_root_base, firmware_root_length;


#define HV_AGX_SCANOUT_GUEST_VINTID                                      \
    HV_AGX_ABI_ADMISSION_SYNTHETIC_SCANOUT_GUEST_INTID

u64 hv_ipa_to_pa(u64 ipa);

static bool local_reserve_translate(void *opaque, uint64_t ipa, uint64_t *pa)
{
    (void)opaque;
    *pa = hv_ipa_to_pa(ipa);
    return *pa != 0;
}

static void local_reserve_select(void)
{
    const struct hv_autonomous_layout *layout = &J313_AUTONOMOUS_LAYOUT;
    struct hv_agx_local_range excluded[2];
    struct hv_agx_local_failure failure = {0};
    static const char *const failure_names[] = {
        "ok", "argument", "overlap", "unmapped", "pa-alignment",
        "pa-limit", "noncontiguous",
    };
    u64 low_end, candidate, ram_end, shared_base = 0, shared_size = 0;
    int sgx = adt_path_offset(adt, "/arm-io/sgx");

    local_reserve = (struct hv_agx_local_receipt){0};
    if (sgx < 0 || layout->low_mem_pa > ~0ULL - layout->low_mem_size ||
        cur_boot_args.phys_base > ~0ULL - cur_boot_args.mem_size ||
        ADT_GETPROP(adt, sgx, "gfx-shared-region-base", &shared_base) < 0 ||
        ADT_GETPROP(adt, sgx, "gfx-shared-region-size", &shared_size) < 0 ||
        !shared_size)
        return;
    low_end = layout->low_mem_pa + layout->low_mem_size;
    ram_end = cur_boot_args.phys_base + cur_boot_args.mem_size;
    if (ram_end > layout->ram_end)
        ram_end = layout->ram_end;
    if (low_end <= layout->phys_base || low_end >= ram_end ||
        shared_base > ~0ULL - shared_size ||
        low_end > ~0ULL - (HV_AGX_LOCAL_BYTES - 1))
        return;
    candidate = (low_end + HV_AGX_LOCAL_BYTES - 1) &
                ~(HV_AGX_LOCAL_BYTES - 1);
    if (candidate >= ram_end || HV_AGX_LOCAL_BYTES > ram_end - candidate)
        return;
    /* The current J313 guest layout occupies all RAM below the low-window
     * backing end: firmware, framebuffer, RAMDisk, and the alias backing.
     * Mu independently rejects any PEI HOB overlap before reserving pages. */
    excluded[0] = (struct hv_agx_local_range){layout->phys_base,
                                               low_end - layout->phys_base};
    excluded[1] = (struct hv_agx_local_range){shared_base, shared_size};
    /* Only the first aligned gap after the low alias is eligible. If it
     * fails stage-2 or firmware validation, do not drift into unknown
     * upper carveouts on this boot. */
    if (hv_agx_local_select_detailed(candidate, HV_AGX_LOCAL_BYTES,
                                     excluded, 2, local_reserve_translate, NULL,
                                     &local_reserve, &failure))
        printf("HV: AGX local reserve v%u IPA=0x%lx PA=0x%lx bytes=0x%lx\n",
               HV_AGX_LOCAL_ABI_VERSION, local_reserve.guest_ipa,
               local_reserve.host_pa, local_reserve.bytes);
    else
        printf("HV: AGX local reserve unavailable reason=%s IPA=0x%lx PA=0x%lx "
               "candidate=0x%lx RAM-end=0x%lx; G3 local memory fails closed\n",
               failure.reason < sizeof(failure_names) / sizeof(failure_names[0]) ?
                   failure_names[failure.reason] : "unknown",
               failure.ipa, failure.pa, candidate, ram_end);
}

static bool scanout_translate(void *opaque, uint64_t ipa, uint64_t *pa)
{
    (void)opaque;
    if (!pa)
        return false;
    *pa = hv_ipa_to_pa(ipa);
    return *pa != 0;
}

static bool scanout_is_ram(void *opaque, uint64_t pa, uint64_t size)
{
    uint64_t base = cur_boot_args.phys_base;
    uint64_t length = cur_boot_args.mem_size;

    (void)opaque;
    return size && pa >= base && size <= length && pa - base <= length - size;
}

static bool scanout_reserve_iova(void *opaque, uint64_t size, uint64_t alignment,
                                 uint64_t *iova)
{
    (void)opaque;
    return display_scanout_reserve_iova(size, alignment, iova);
}

static void scanout_free_iova(void *opaque, uint64_t iova, uint64_t size)
{
    (void)opaque;
    display_scanout_free_iova(iova, size);
}

static bool scanout_map(void *opaque, enum hv_agx_scanout_dart dart,
                        uint64_t iova, uint64_t pa, uint64_t size)
{
    (void)opaque;
    return display_scanout_map((unsigned)dart, iova, pa, size);
}

static void scanout_unmap(void *opaque, enum hv_agx_scanout_dart dart,
                          uint64_t iova, uint64_t size)
{
    (void)opaque;
    display_scanout_unmap((unsigned)dart, iova, size);
}

static bool scanout_present_begin(void *opaque, uint64_t surface_iova,
                                  const struct hv_agx_scanout_request *request,
                                  uint64_t *cookie)
{
    (void)opaque;
    return request && display_scanout_present_begin(
                          surface_iova, request->Width, request->Height,
                          request->Stride, cookie);
}

#ifdef EXP806_SCANOUT_PATTERN
static bool scanout_diagnostic_fill(void *opaque, uint64_t surface_pa,
                                     uint64_t surface_iova,
                                     const struct hv_agx_scanout_request *request)
{
    void *surface = (void *)(uintptr_t)surface_pa;
    uint32_t *pixels = surface;
    uint32_t width;

    (void)opaque;
    if (!request || !surface_pa || !surface_iova ||
        request->Width != HV_AGX_SCANOUT_J313_WIDTH ||
        request->Height != HV_AGX_SCANOUT_J313_HEIGHT ||
        request->Stride != HV_AGX_SCANOUT_J313_STRIDE ||
        request->Format != HV_AGX_SCANOUT_FORMAT_BGRA8888 ||
        request->SurfaceSize != HV_AGX_SCANOUT_J313_SURFACE_SIZE ||
        !scanout_is_ram(NULL, surface_pa, request->SurfaceSize) ||
        !hv_agx_scanout_fill_bgra_stripes(
            surface, request->Width, request->Height,
            request->Stride, request->SurfaceSize))
        return false;
    dc_cvac_range(surface, request->SurfaceSize);
    dma_wmb();
    width = request->Width;
    printf("EXP806_PATTERN pool_ipa=0x%lx surface_offset=0x%lx surface_pa=0x%lx "
           "surface_iova=0x%lx bytes=0x%lx BGRA=%08x,%08x,%08x,%08x\n",
           request->PoolIpa, request->SurfaceOffset, surface_pa,
           surface_iova, request->SurfaceSize,
           pixels[0], pixels[width / 4u], pixels[width / 2u],
           pixels[width * 3u / 4u]);
    return true;
}
#endif

#if defined(EXP807_SCANOUT_SNAPSHOT) || defined(EXP808_SCANOUT_DELAYED)
static void scanout_snapshot_report(const char *label, uint64_t surface_pa,
                                    uint64_t surface_iova,
                                    const struct hv_agx_scanout_request *request)
{
    struct hv_agx_scanout_pixel_stats stats;
    const void *surface = (const void *)(uintptr_t)surface_pa;
    if (!request || !surface_pa || !surface_iova ||
        !scanout_is_ram(NULL, surface_pa, request->SurfaceSize) ||
        !hv_agx_scanout_pixel_stats(surface, request->Width, request->Height,
                                     request->Stride, request->SurfaceSize,
                                     &stats)) {
        printf("%s invalid seq=%lu pa=0x%lx iova=0x%lx\n", label,
               request ? request->Sequence : 0, surface_pa, surface_iova);
        return;
    }
    printf("%s seq=%lu pool_ipa=0x%lx offset=0x%lx pa=0x%lx "
           "iova=0x%lx pixels=%lu nonzero=%lu avg_bgra=%lu,%lu,%lu,%lu "
           "hash=%016lx corners=%08x,%08x,%08x,%08x cache_clean=0\n",
           label, request->Sequence, request->PoolIpa, request->SurfaceOffset,
           surface_pa, surface_iova, stats.pixel_count, stats.nonzero_pixels,
           stats.channel_sum[0] / stats.pixel_count,
           stats.channel_sum[1] / stats.pixel_count,
           stats.channel_sum[2] / stats.pixel_count,
           stats.channel_sum[3] / stats.pixel_count, stats.hash,
           stats.corners[0], stats.corners[1], stats.corners[2],
           stats.corners[3]);
}

static void scanout_diagnostic_snapshot(void *opaque, uint64_t surface_pa,
                                         uint64_t surface_iova,
                                         const struct hv_agx_scanout_request *request)
{
    (void)opaque;
    scanout_snapshot_report("EXP807_SNAPSHOT", surface_pa, surface_iova,
                            request);
}
#ifdef EXP808_SCANOUT_DELAYED
static uint64_t scanout_now_ms(void *opaque)
{
    (void)opaque;
    return ticks_to_msecs(get_ticks());
}

static void scanout_diagnostic_late_snapshot(
    void *opaque, uint64_t surface_pa, uint64_t surface_iova,
    const struct hv_agx_scanout_request *request)
{
    (void)opaque;
    scanout_snapshot_report("EXP808_LATE_SNAPSHOT", surface_pa, surface_iova,
                            request);
}
#endif
#endif

static enum hv_agx_scanout_async_result scanout_present_poll(
    void *opaque, uint64_t cookie, uint32_t *applied_swap_id)
{
    int result;

    (void)opaque;
    result = display_scanout_present_poll(cookie, applied_swap_id);
    return result > 0 ? HV_AGX_SCANOUT_ASYNC_APPLIED
                      : result == 0 ? HV_AGX_SCANOUT_ASYNC_PENDING
                                    : HV_AGX_SCANOUT_ASYNC_FAILED;
}

static enum hv_agx_scanout_latch_result scanout_present_latch_poll(
    void *opaque, uint32_t expected_swap_id)
{
    int result;

    (void)opaque;
    result = display_scanout_latch_poll(expected_swap_id);
    return result > 0 ? HV_AGX_SCANOUT_LATCHED
                      : result == 0 ? HV_AGX_SCANOUT_LATCH_PENDING
                                    : HV_AGX_SCANOUT_LATCH_FAILED;
}

static bool scanout_quiesce_begin(void *opaque, uint64_t *cookie)
{
    (void)opaque;
    return display_scanout_quiesce_begin(cookie);
}

static enum hv_agx_scanout_async_result scanout_quiesce_poll(void *opaque,
                                                              uint64_t cookie)
{
    int result;

    (void)opaque;
    result = display_scanout_quiesce_poll(cookie);
    return result > 0 ? HV_AGX_SCANOUT_ASYNC_APPLIED
                      : result == 0 ? HV_AGX_SCANOUT_ASYNC_PENDING
                                    : HV_AGX_SCANOUT_ASYNC_FAILED;
}

static const struct hv_agx_scanout_platform_ops scanout_ops = {
    .translate = scanout_translate,
    .is_ram = scanout_is_ram,
    .reserve_iova = scanout_reserve_iova,
    .free_iova = scanout_free_iova,
    .map = scanout_map,
    .unmap = scanout_unmap,
#ifdef EXP806_SCANOUT_PATTERN
    .diagnostic_fill = scanout_diagnostic_fill,
#endif
#if defined(EXP807_SCANOUT_SNAPSHOT) || defined(EXP808_SCANOUT_DELAYED)
    .diagnostic_snapshot = scanout_diagnostic_snapshot,
#endif
#ifdef EXP808_SCANOUT_DELAYED
    .now_ms = scanout_now_ms,
    .diagnostic_late_snapshot = scanout_diagnostic_late_snapshot,
#endif
    .present_begin = scanout_present_begin,
    .present_poll = scanout_present_poll,
    .present_latch_poll = scanout_present_latch_poll,
    .quiesce_begin = scanout_quiesce_begin,
    .quiesce_poll = scanout_quiesce_poll,
};

static bool handle_agx_power_broker(struct exc_info *ctx, u64 addr, u64 *value, bool write,
                                    int width)
{
    u64 offset;
    bool handled;

    (void)ctx;
    if (addr < HV_AGX_G2_POWER_BROKER_BASE ||
        addr >= HV_AGX_G2_POWER_BROKER_BASE + HV_AGX_G2_POWER_BROKER_SIZE)
        return false;

    offset = addr - HV_AGX_G2_POWER_BROKER_BASE;
    if (offset >= HV_AGX_LOCAL_MMIO_OFFSET &&
        offset < HV_AGX_LOCAL_MMIO_OFFSET + HV_AGX_LOCAL_MMIO_BYTES)
        return hv_agx_local_receipt_mmio(&local_reserve, offset, write,
                                          (unsigned)width, value);
    if (offset >= AGX_HWDATA_RECEIPT_OFFSET &&
        offset < AGX_HWDATA_RECEIPT_OFFSET+AGX_HWDATA_RECEIPT_BYTES) {
        spin_lock(&broker_lock);
        handled = hv_agx_retained_platform_profile(offset-AGX_HWDATA_RECEIPT_OFFSET,
                    value,write,(unsigned)width,broker.state == HV_AGX_POWER_ON);
        spin_unlock(&broker_lock);
        return handled;
    }
    if (offset >= AGX_FW_IO_OFFSET && offset < AGX_FW_IO_OFFSET+AGX_FW_IO_BYTES) {
        spin_lock(&broker_lock);
        handled = hv_agx_retained_platform_io(offset-AGX_FW_IO_OFFSET,value,
                    write,(unsigned)width,broker.state == HV_AGX_POWER_ON);
        spin_unlock(&broker_lock);
        return handled;
    }
    if (offset >= AGX_RR_OFFSET && offset < AGX_RR_OFFSET + AGX_RR_WINDOW) {
        spin_lock(&broker_lock);
        handled = hv_agx_retained_platform_mmio(offset - AGX_RR_OFFSET, value,
                    write, (unsigned)width, broker.state == HV_AGX_POWER_ON);
        spin_unlock(&broker_lock);
        return handled;
    }
    if (offset >= AGX_GPUVA_V5_OFFSET &&
        offset < AGX_GPUVA_V5_OFFSET + AGX_GPUVA_V5_WINDOW) {
        spin_lock(&broker_lock);
        handled = hv_agx_retained_platform_gpuva_v5(
            offset - AGX_GPUVA_V5_OFFSET, value, write, (unsigned)width,
            broker.state == HV_AGX_POWER_ON);
        spin_unlock(&broker_lock);
        return handled;
    }
    if (offset >= AGX_FW_PREFIX_OFFSET && offset < AGX_FW_PREFIX_OFFSET + AGX_FW_PREFIX_SIZE) {
        /* Deprecated prefix-copy ABI: never disclose private entries. */
        if (write || width < 0 || width > 3 || (offset & ((1u << width)-1)))
            return false;
        *value = 0;
        return true;
    }
    if (offset >= HV_AGX_SCANOUT_MMIO_OFFSET &&
        offset < HV_AGX_SCANOUT_MMIO_OFFSET + HV_AGX_SCANOUT_MMIO_SIZE) {
        spin_lock(&broker_lock);
        handled = hv_agx_scanout_broker_mmio(
            &scanout_broker, offset - HV_AGX_SCANOUT_MMIO_OFFSET, value, write,
            (unsigned)width);
        spin_unlock(&broker_lock);
        return handled;
    }
    if (offset >= HV_AGX_CONFIG_MMIO_OFFSET &&
        offset < HV_AGX_CONFIG_MMIO_OFFSET + sizeof(config_snapshot)) {
        if (!config_snapshot_valid)
            return false;
        return hv_agx_config_snapshot_mmio(
            &config_snapshot, offset - HV_AGX_CONFIG_MMIO_OFFSET,
            value, write, (unsigned)width);
    }

    spin_lock(&broker_lock);
    if (write && offset == HV_AGX_POWER_REG_COMMAND && width == 2 &&
        *value == HV_AGX_POWER_CMD_OFF && !hv_agx_retained_can_power_off()) {
        broker.result = HV_AGX_POWER_RESULT_BUSY;
        spin_unlock(&broker_lock);
        return true;
    }
    handled = hv_agx_power_broker_mmio(&broker, offset, value,
                                       write, (unsigned)width);
    if (handled && write && offset == HV_AGX_POWER_REG_COMMAND) {
        struct hv_agx_power_snapshot snapshot;

        hv_agx_power_broker_snapshot(&broker, &snapshot);
        printf("HV: AGX power receipt seq=%lu cmd=%lu state=%u result=%u\n",
               snapshot.receipt_sequence, *value, snapshot.state, snapshot.result);
    }
    spin_unlock(&broker_lock);
    return handled;
}

bool hv_agx_g2_resources_map(void)
{
    int ret;

    /* Assisted launch first calls this before Python's final pt_update().
     * Retry the receipt when that update has installed guest RAM stage-2
     * mappings; leave the already published MMIO hook untouched. */
    if (resources_mapped) {
        if (local_reserve.bytes != HV_AGX_LOCAL_BYTES)
            local_reserve_select();
        return true;
    }

    ret = hv_map_hook(HV_AGX_G2_GPU_BASE, hv_agx_retained_gpu_region, HV_AGX_G2_GPU_SIZE);
    if (ret < 0) {
        printf("HV: AGX gpu-region stage-2 map failed (%d)\n", ret);
        return false;
    }

    hv_agx_power_broker_init(&broker, hv_agx_power_j313_ops(), NULL);
    local_reserve_select();
    hv_agx_scanout_service_init(&scanout_service, &scanout_ops, NULL);
    if (display_start_dcp() < 0) {
        printf("HV: AGX scanout DCP backend unavailable; requests fail closed\n");
        hv_agx_scanout_broker_init(&scanout_broker);
    } else if (display_scanout_latch_source_proven()) {
        hv_agx_scanout_broker_init_v2(&scanout_broker, true);
        printf("HV: AGX scanout ABI v2 enabled with proven IOMFB latch source\n");
    } else {
        hv_agx_scanout_broker_init(&scanout_broker);
        printf("HV: AGX scanout stays ABI v1 without a proven latch source\n");
    }
    config_snapshot_valid = hv_agx_config_snapshot_from_adt(adt, &config_snapshot);
    {
        int sgx = adt_path_offset(adt, "/arm-io/sgx");
        u64 base = 0, length = 0;
        u64 ram = ALIGN_DOWN(cur_boot_args.phys_base, BIT(32));
        u64 size = mem_size_actual;
        if (sgx >= 0 && ADT_GETPROP(adt, sgx, "gfx-shared-region-base", &base) >= 0 &&
            ADT_GETPROP(adt, sgx, "gfx-shared-region-size", &length) >= 0 &&
            AgxFwPrefixGeometry(base, length) && base >= ram &&
            length <= size && base - ram <= size - length) {
            firmware_root_base = base;
            firmware_root_length = length;
            if (!hv_agx_retained_platform_init(base, length))
                return false;
            printf("HV: AGX retained-root broker v%u root=0x%lx size=0x%lx broker+0x%x\n",
                   AGX_RR_ABI_VERSION, base, length, AGX_RR_OFFSET);
        }
    }
    if (!config_snapshot_valid)
        printf("HV: AGX boot config snapshot unavailable; firmware start must fail closed\n");
    ret = hv_map_hook(HV_AGX_G2_POWER_BROKER_BASE, handle_agx_power_broker,
                      HV_AGX_G2_POWER_BROKER_SIZE);
    if (ret < 0) {
        printf("HV: AGX power broker map failed (%d)\n", ret);
        return false;
    }

    resources_mapped = true;
    printf("HV: AGX gpu-region mapped at 0x%lx..0x%lx\n",
           (u64)HV_AGX_G2_GPU_BASE,
           (u64)(HV_AGX_G2_GPU_BASE + HV_AGX_G2_GPU_SIZE));
    printf("HV: AGX power broker mapped at 0x%lx..0x%lx (ABI %u)\n",
           (u64)HV_AGX_G2_POWER_BROKER_BASE,
           (u64)(HV_AGX_G2_POWER_BROKER_BASE + HV_AGX_G2_POWER_BROKER_SIZE),
           HV_AGX_POWER_ABI_VERSION);
    if (config_snapshot_valid)
        printf("HV: AGX boot config snapshot v%u at broker+0x%x (%u pstates, %u scalars, mask=0x%llx)\n",
               HV_AGX_CONFIG_ABI_VERSION, HV_AGX_CONFIG_MMIO_OFFSET,
               config_snapshot.perf_state_count,
               (unsigned)__builtin_popcountll(config_snapshot.scalar_presence),
               (unsigned long long)config_snapshot.scalar_presence);
    return true;
}

void hv_agx_scanout_service_run_once(void)
{
    bool inject_scanout = false;

    if (!resources_mapped || !spin_try_lock(&service_lock))
        return;
    if (spin_try_lock(&broker_lock)) {
        (void)hv_agx_scanout_service_step(&scanout_service, &scanout_broker);
        if (hv_vgic3_irq_enabled(HV_AGX_SCANOUT_GUEST_VINTID) &&
            hv_vgic3_get_free_lr() >= 0)
            inject_scanout =
                hv_agx_scanout_broker_take_irq_edge(&scanout_broker);
        spin_unlock(&broker_lock);
    }
    if (inject_scanout)
        hv_vgic3_inject_irq(
            HV_AGX_SCANOUT_GUEST_VINTID,
            hv_vgic3_get_priority(HV_AGX_SCANOUT_GUEST_VINTID), false, true,
            false, 0);
    spin_unlock(&service_lock);
}
