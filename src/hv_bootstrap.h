/* SPDX-License-Identifier: MIT */

#ifndef HV_BOOTSTRAP_H
#define HV_BOOTSTRAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hv_bootstrap_manifest.h"

enum hv_bootstrap_attempt {
    HV_BOOTSTRAP_ABSENT = 0,
    HV_BOOTSTRAP_HANDLED,
    HV_BOOTSTRAP_ATTEMPT_FAILED,
};

enum hv_bootstrap_result {
    HV_BOOTSTRAP_RESULT_OK = 0,
    HV_BOOTSTRAP_RESULT_INVALID,
    HV_BOOTSTRAP_RESULT_ALLOCATE_FAILED,
    HV_BOOTSTRAP_RESULT_DECOMPRESS_FAILED,
    HV_BOOTSTRAP_RESULT_SOURCE_SIZE,
    HV_BOOTSTRAP_RESULT_DESTINATION_SIZE,
    HV_BOOTSTRAP_RESULT_CRC_MISMATCH,
    HV_BOOTSTRAP_RESULT_CHAINLOAD_FAILED,
};

struct hv_bootstrap_ops {
    void *(*allocate)(size_t size, size_t alignment, void *opaque);
    bool (*decompress)(const void *source, uint32_t *source_size, void *destination,
                       uint32_t *destination_size, void *opaque);
    uint32_t (*crc32)(const void *data, size_t size, void *opaque);
    int (*chainload)(void *image, size_t size, void *opaque);
};

enum hv_bootstrap_result hv_bootstrap_prepare_with_ops(
    const struct hv_bootstrap_payload *payload, const struct hv_bootstrap_ops *ops,
    void *opaque);
enum hv_bootstrap_attempt hv_bootstrap_attempt_from_manifest_error(
    enum hv_bootstrap_error error);
enum hv_bootstrap_attempt hv_bootstrap_chainload_if_present(bool *usb_up);

#endif
