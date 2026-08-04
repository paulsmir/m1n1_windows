/* SPDX-License-Identifier: MIT */

#include "hv_autonomous.h"

#include "adt.h"
#include "heapblock.h"
#include "hv.h"
#include "hv_autonomous_layout.generated.h"
#include "hv_autonomous_profile.h"
#include "hv_vgic.h"
#include "display.h"
#include "memory.h"
#include "minilzlib/minlzma.h"
#include "string.h"
#include "tinf/tinf.h"
#include "types.h"
#include "utils.h"
#include "xnuboot.h"

struct hv_autonomous_runtime {
    void *firmware;
    u32 firmware_size;
    struct hv_autonomous_profile profile;
};

static bool map_arm_io_ranges(void)
{
    int node = adt_path_offset(adt, "/arm-io");
    u32 ranges_len = 0;
    const u32 *ranges;

    if (node < 0)
        return false;
    ranges = adt_getprop(adt, node, "ranges", &ranges_len);
    if (!ranges || ranges_len % 24)
        return false;

    for (u32 offset = 0; offset < ranges_len; offset += 24, ranges += 6) {
        u64 base = ranges[2] | ((u64)ranges[3] << 32);
        u64 size = ranges[4] | ((u64)ranges[5] << 32);

        if (!size || hv_map_hw(base, base, size))
            return false;
    }
    return true;
}

static bool prepare_boot_data(struct hv_autonomous_runtime *runtime,
                              struct hv_autonomous_status *status)
{
    const struct hv_autonomous_layout *layout = &J313_AUTONOMOUS_LAYOUT;
    struct boot_args guest_args = cur_boot_args;
    u64 firmware_end = layout->firmware_base + runtime->firmware_size;
    u64 boot_args_end = layout->boot_args_base + layout->boot_args_size;

    if (cur_boot_args.devtree_size > layout->adt_max_size ||
        runtime->firmware_size > layout->firmware_max_size)
        return false;

    memcpy((void *)layout->firmware_base, runtime->firmware, runtime->firmware_size);
    memcpy((void *)layout->adt_base, adt, cur_boot_args.devtree_size);

    guest_args.phys_base = layout->phys_base;
    guest_args.mem_size = layout->ram_end - layout->phys_base;
    guest_args.virt_base = 0xfffffe0010000000ULL + (layout->phys_base & (SZ_32M - 1));
    guest_args.devtree = (void *)(guest_args.virt_base + layout->adt_base - layout->phys_base);
    guest_args.devtree_size = cur_boot_args.devtree_size;
    guest_args.top_of_kernel_data = firmware_end > boot_args_end ? firmware_end : boot_args_end;
    guest_args.video.base = layout->virtual_fb_base;
    guest_args.video.display = 1;
    guest_args.video.stride = layout->virtual_fb_stride;
    guest_args.video.width = layout->virtual_fb_width;
    guest_args.video.height = layout->virtual_fb_height;
    guest_args.video.depth = 32;
    if (guest_args.revision <= 1)
        guest_args.rv1.mem_size_actual = guest_args.mem_size;
    else if (guest_args.revision == 2)
        guest_args.rv2.mem_size_actual = guest_args.mem_size;
    else
        guest_args.rv3.mem_size_actual = guest_args.mem_size;
    memcpy((void *)layout->boot_args_base, &guest_args, sizeof(guest_args));

    dc_cvau_range((void *)layout->firmware_base, runtime->firmware_size);
    ic_ivau_range((void *)layout->firmware_base, runtime->firmware_size);
    dc_cvau_range((void *)layout->adt_base, cur_boot_args.devtree_size);
    dc_cvau_range((void *)layout->boot_args_base, sizeof(guest_args));
    status->firmware_entry = layout->firmware_base;
    return true;
}

static bool map_stage2(void)
{
    const struct hv_autonomous_layout *layout = &J313_AUTONOMOUS_LAYOUT;

    hv_init();
    if (hv_map_hw(layout->phys_base, layout->phys_base, layout->ram_end - layout->phys_base))
        return false;
    if (hv_map_hw(layout->low_mem_ipa, layout->low_mem_pa, layout->low_mem_size))
        return false;
    return map_arm_io_ranges();
}

static bool map_vuart_from_adt(void)
{
    int path[8];
    int node = adt_path_offset_trace(adt, "/arm-io/uart0", path);
    u64 base;
    const u32 *interrupts;
    u32 length;

    if (node < 0 || adt_get_reg(adt, path, "reg", 0, &base, NULL))
        return false;
    interrupts = adt_getprop(adt, node, "interrupts", &length);
    if (!interrupts || length < sizeof(*interrupts))
        return false;
    return hv_map_vuart(base, interrupts[0], IODEV_UART);
}

static bool runtime_stage(enum hv_autonomous_stage stage,
                          const struct hv_autonomous_payload *payload,
                          struct hv_autonomous_status *status, void *opaque)
{
    struct hv_autonomous_runtime *runtime = opaque;
    const struct hv_autonomous_layout *layout = &J313_AUTONOMOUS_LAYOUT;

    switch (stage) {
        case HV_AUTONOMOUS_STAGE_VALIDATE:
            return hv_autonomous_profile_decode(payload->flags, &runtime->profile) &&
                   payload->layout_version == layout->layout_version &&
                   payload->uncompressed_size <= layout->firmware_max_size &&
                   payload->compressed_size <= UINT32_MAX &&
                   payload->uncompressed_size <= UINT32_MAX;
        case HV_AUTONOMOUS_STAGE_DECOMPRESS: {
            u32 source_size = payload->compressed_size;
            u32 destination_size = payload->uncompressed_size;

            runtime->firmware = heapblock_alloc_aligned(destination_size, SZ_16K);
            if (!runtime->firmware ||
                !XzDecode((u8 *)payload->compressed, &source_size, runtime->firmware,
                          &destination_size) ||
                source_size != payload->compressed_size ||
                destination_size != payload->uncompressed_size ||
                tinf_crc32(runtime->firmware, destination_size) != payload->crc32)
                return false;
            runtime->firmware_size = destination_size;
            return true;
        }
        case HV_AUTONOMOUS_STAGE_BOOT_DATA:
            return prepare_boot_data(runtime, status);
        case HV_AUTONOMOUS_STAGE_STAGE2:
            return map_stage2();
        case HV_AUTONOMOUS_STAGE_VGIC:
            return true; // hv_init() owns vGIC/PSCI initialization.
        case HV_AUTONOMOUS_STAGE_PCI_NVME:
            return hv_pci_init(layout->pci_ecam, layout->pci_bar_window,
                               layout->nvme_vintid);
        case HV_AUTONOMOUS_STAGE_XHCI:
            // Broad /arm-io pass-through is installed after hv_init(); restore
            // the diagnostic/DART hook over the physical xHCI page last.
            return hv_vgic_rearm_j313_xhci_trace();
        case HV_AUTONOMOUS_STAGE_VUART:
            return map_vuart_from_adt();
        case HV_AUTONOMOUS_STAGE_READY:
            memset((void *)layout->virtual_fb_base, 0,
                   (u64)layout->virtual_fb_stride * layout->virtual_fb_height);
            if (runtime->profile.physical_display &&
                display_prepare_guest_surface(
                    layout->virtual_fb_base,
                    (u64)layout->virtual_fb_stride * layout->virtual_fb_height,
                    layout->virtual_fb_width, layout->virtual_fb_height,
                    layout->virtual_fb_stride, 32) != 1)
                return false;
            if (runtime->profile.virtual_display &&
                !hv_configure_fb_stream(
                    layout->virtual_fb_base,
                    (u64)layout->virtual_fb_stride * layout->virtual_fb_height,
                    layout->virtual_fb_width, layout->virtual_fb_height,
                    layout->virtual_fb_stride))
                return false;
            printf("Standalone: display physical=%u virtual=%u telemetry=%u\n",
                   runtime->profile.physical_display, runtime->profile.virtual_display,
                   runtime->profile.telemetry);
            return true;
        case HV_AUTONOMOUS_STAGE_ENTERED: {
            u64 regs[4] = {layout->boot_args_base, 0, 0, 0};
            hv_start((void *)status->firmware_entry, regs);
            return false; // A successful guest entry does not return.
        }
        case HV_AUTONOMOUS_STAGE_COUNT:
            return false;
    }
    return false;
}

enum hv_autonomous_result hv_autonomous_prepare(const struct hv_autonomous_payload *payload,
                                                struct hv_autonomous_status *status)
{
    static const struct hv_autonomous_ops ops = {
        .callbacks = {
            runtime_stage, runtime_stage, runtime_stage, runtime_stage, runtime_stage,
            runtime_stage, runtime_stage, runtime_stage, runtime_stage, runtime_stage,
        },
    };
    struct hv_autonomous_runtime runtime = {0};

    return hv_autonomous_prepare_with_ops(payload, status, &ops, &runtime);
}
