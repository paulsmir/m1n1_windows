/* SPDX-License-Identifier: MIT */

#ifndef DCP_IOMFB_BOOTSTRAP_H
#define DCP_IOMFB_BOOTSTRAP_H

#ifdef DCP_IOMFB_BOOTSTRAP_HOST_TEST
#include <stdbool.h>
#include <stdint.h>
#else
#include "types.h"
#endif

enum dcp_iomfb_boot_state {
    DCP_IOMFB_BOOT_OFF = 0,
    DCP_IOMFB_BOOT_BOOTSTRAP,
    DCP_IOMFB_BOOT_POST_INIT,
    DCP_IOMFB_BOOT_ACTIVE,
    DCP_IOMFB_BOOT_POWERED,
    DCP_IOMFB_BOOT_MODESET,
    DCP_IOMFB_BOOT_FAILED,
};

typedef bool (*dcp_iomfb_boot_call_fn)(void *opaque, const char tag[4],
                                       const void *input, uint32_t input_size,
                                       void *output, uint32_t output_size);
typedef int (*dcp_iomfb_boot_platform_fn)(void *opaque,
                                         unsigned int callback_id,
                                         const void *input,
                                         uint32_t input_size, void *output,
                                         uint32_t output_size);

struct dcp_iomfb_bootstrap {
    dcp_iomfb_boot_call_fn call;
    dcp_iomfb_boot_platform_fn platform;
    void *opaque;
    enum dcp_iomfb_boot_state state;
    bool main_display;
};

void dcp_iomfb_bootstrap_init(struct dcp_iomfb_bootstrap *bootstrap,
                              dcp_iomfb_boot_call_fn call,
                              dcp_iomfb_boot_platform_fn platform,
                              void *opaque);
bool dcp_iomfb_bootstrap_start_through_color_remap(
    struct dcp_iomfb_bootstrap *bootstrap);
bool dcp_iomfb_bootstrap_start_through_video_power_savings(
    struct dcp_iomfb_bootstrap *bootstrap);
bool dcp_iomfb_bootstrap_start_through_first_client_open(
    struct dcp_iomfb_bootstrap *bootstrap);
bool dcp_iomfb_bootstrap_start(struct dcp_iomfb_bootstrap *bootstrap);
bool dcp_iomfb_bootstrap_power_on(struct dcp_iomfb_bootstrap *bootstrap);
bool dcp_iomfb_bootstrap_modeset(struct dcp_iomfb_bootstrap *bootstrap,
                                 uint32_t color_mode_id,
                                 uint32_t timing_mode_id);
int dcp_iomfb_bootstrap_callback(struct dcp_iomfb_bootstrap *bootstrap,
                                 const char tag[4], const void *input,
                                 uint32_t input_size, void *output,
                                 uint32_t output_size);
enum dcp_iomfb_boot_state
dcp_iomfb_bootstrap_state(const struct dcp_iomfb_bootstrap *bootstrap);
bool dcp_iomfb_bootstrap_main_display(
    const struct dcp_iomfb_bootstrap *bootstrap);

#endif
