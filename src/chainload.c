/* SPDX-License-Identifier: MIT */

#include "../build/build_cfg.h"

#include "chainload.h"
#include "adt.h"
#include "chainload_layout.h"
#include "malloc.h"
#include "memory.h"
#include "nvme.h"
#include "smp.h"
#include "string.h"
#include "types.h"
#include "utils.h"
#include "xnuboot.h"

#ifdef CHAINLOADING
int rust_load_image(const char *spec, void **image, size_t *size);
#endif

extern u8 _chainload_stub_start[];
extern u8 _chainload_stub_end[];

static int chainload_collect_rvbars(u64 addrs[MAX_CPUS], size_t *count)
{
    int node = adt_path_offset(adt, "/cpus");

    *count = 0;
    if (node < 0) {
        printf("chainload: /cpus not found\n");
        return -1;
    }

    ADT_FOREACH_CHILD(adt, node)
    {
        const char *state = adt_getprop(adt, node, "state", NULL);
        u64 cpu_impl_reg[2];

        if (state && strcmp(state, "running") == 0)
            continue;
        if (*count >= MAX_CPUS ||
            ADT_GETPROP_ARRAY(adt, node, "cpu-impl-reg", cpu_impl_reg) < 0) {
            printf("chainload: missing cpu-impl-reg for %s\n", adt_get_name(adt, node));
            return -1;
        }
        addrs[(*count)++] = cpu_impl_reg[0];
    }

    return 0;
}

static void chainload_prepare_rvbars(const u64 addrs[MAX_CPUS], size_t count, u64 entry)
{
    u64 rvbar = entry & ~0xfffULL;

    printf("chainload: Setting secondary CPU RVBARs...\n");
    for (size_t i = 0; i < count; i++) {
        printf("chainload:   [0x%lx] = 0x%lx\n", addrs[i], rvbar);
        write64(addrs[i], rvbar);
    }
    sysop("dmb sy");
}

int chainload_image(void *image, size_t size, char **vars, size_t var_cnt)
{
    u64 new_base = (u64)_base;
    size_t image_and_vars = size;
    struct chainload_layout layout;
    u64 rvbar_addrs[MAX_CPUS];
    size_t rvbar_count;
    u64 sepfw[2];
    u64 bootargs[2];
    u64 preoslog[2] = {0};
    u32 preoslog_prop_size = 0;
    bool has_preoslog = false;
    bool sepfw_updated = false;
    bool bootargs_updated = false;
    void *new_image;

    printf("chainload: Preparing image...\n");

    // m1n1 variables
    for (size_t i = 0; i < var_cnt; i++) {
        size_t len = strlen(vars[i]);
        if (len == SIZE_MAX || image_and_vars > SIZE_MAX - len - 1) {
            printf("chainload: variable size overflow\n");
            return -1;
        }
        image_and_vars += len + 1;
    }
    if (image_and_vars > SIZE_MAX - 4) {
        printf("chainload: image size overflow\n");
        return -1;
    }
    image_and_vars += 4;

    int anode = adt_path_offset(adt, "/chosen/memory-map");
    if (anode < 0) {
        printf("chainload: /chosen/memory-map not found\n");
        return -1;
    }
    if (ADT_GETPROP_ARRAY(adt, anode, "SEPFW", sepfw) < 0) {
        printf("chainload: Failed to find SEPFW\n");
        return -1;
    }
    if (ADT_GETPROP_ARRAY(adt, anode, "BootArgs", bootargs) < 0) {
        printf("chainload: Failed to find BootArgs\n");
        return -1;
    }
    const u64 *preoslog_prop = adt_getprop(adt, anode, "preoslog", &preoslog_prop_size);
    if (preoslog_prop) {
        if (preoslog_prop_size != sizeof(preoslog)) {
            printf("chainload: Invalid preoslog property\n");
            return -1;
        }
        memcpy(preoslog, preoslog_prop, sizeof(preoslog));
        has_preoslog = true;
    }
    if (chainload_collect_rvbars(rvbar_addrs, &rvbar_count) < 0)
        return -1;

    size_t stub_size = _chainload_stub_end - _chainload_stub_start;
    if (!chainload_layout_compute(image_and_vars, sepfw[1], preoslog[1], stub_size,
                                  &layout)) {
        printf("chainload: image layout overflow\n");
        return -1;
    }

    printf("chainload: Total image size: 0x%lx\n", layout.copy_size);

    new_image = malloc(layout.allocation_size);
    if (!new_image) {
        printf("chainload: allocation failed\n");
        return -1;
    }

    // Copy m1n1
    memcpy(new_image, image, size);

    // Add vars
    u8 *p = new_image + size;
    for (size_t i = 0; i < var_cnt; i++) {
        size_t len = strlen(vars[i]);

        memcpy(p, vars[i], len);
        p[len] = '\n';
        p += len + 1;
    }

    // Add end padding
    memset(p, 0, 4);

    // Copy SEPFW
    memcpy(new_image + layout.sepfw_offset, (void *)sepfw[0], sepfw[1]);

    // Copy optional preoslog before its ADT address is changed.
    if (has_preoslog)
        memcpy(new_image + layout.preoslog_offset, (void *)preoslog[0], preoslog[1]);

    // Adjust ADT SEPFW address
    u64 new_sepfw[2] = {new_base + layout.sepfw_offset, sepfw[1]};
    u64 new_bootargs[2] = {new_base + layout.bootargs_offset, CHAINLOAD_BOOTARGS_SIZE};
    u64 new_preoslog[2] = {new_base + layout.preoslog_offset, preoslog[1]};
    if (adt_setprop(adt, anode, "SEPFW", &new_sepfw, sizeof(new_sepfw)) < 0) {
        printf("chainload: Failed to set SEPFW prop\n");
        goto fail;
    }
    sepfw_updated = true;
    if (adt_setprop(adt, anode, "BootArgs", &new_bootargs, sizeof(new_bootargs)) < 0) {
        printf("chainload: Failed to set BootArgs prop\n");
        goto fail;
    }
    bootargs_updated = true;
    if (has_preoslog &&
        adt_setprop(adt, anode, "preoslog", &new_preoslog, sizeof(new_preoslog)) < 0) {
        printf("chainload: Failed to set preoslog prop\n");
        goto fail;
    }

    // Copy bootargs
    struct boot_args *new_boot_args = new_image + layout.bootargs_offset;
    *new_boot_args = cur_boot_args;
    new_boot_args->top_of_kernel_data = new_base + layout.copy_size;

    // Copy chainload stub
    void *stub = new_image + layout.stub_offset;
    memcpy(stub, _chainload_stub_start, stub_size);
    dc_cvau_range(stub, stub_size);
    ic_ivau_range(stub, stub_size);

    chainload_prepare_rvbars(rvbar_addrs, rvbar_count, new_base + 0x800);

    // Set up next stage
    next_stage.entry = stub;
    next_stage.args[0] = new_base + layout.bootargs_offset;
    next_stage.args[1] = (u64)new_image;
    next_stage.args[2] = new_base;
    next_stage.args[3] = layout.copy_size;
    next_stage.args[4] = new_base + 0x800; // m1n1 entrypoint
    next_stage.restore_logo = false;

    return 0;

fail:
    if (bootargs_updated)
        adt_setprop(adt, anode, "BootArgs", &bootargs, sizeof(bootargs));
    if (sepfw_updated)
        adt_setprop(adt, anode, "SEPFW", &sepfw, sizeof(sepfw));
    free(new_image);
    return -1;
}

#ifdef CHAINLOADING

int chainload_load(const char *spec, char **vars, size_t var_cnt)
{
    void *image;
    size_t size;
    int ret;

    if (!nvme_init()) {
        printf("chainload: NVME init failed\n");
        return -1;
    }

    ret = rust_load_image(spec, &image, &size);
    nvme_shutdown();
    if (ret < 0)
        return ret;

    return chainload_image(image, size, vars, var_cnt);
}

#else

int chainload_load(const char *spec, char **vars, size_t var_cnt)
{
    UNUSED(spec);
    UNUSED(vars);
    UNUSED(var_cnt);

    printf("Chainloading files not supported in this build!\n");
    return -1;
}

#endif
