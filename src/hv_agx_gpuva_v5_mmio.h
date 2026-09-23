#ifndef HV_AGX_GPUVA_V5_MMIO_H
#define HV_AGX_GPUVA_V5_MMIO_H
#include <stdint.h>
#include <stdbool.h>
#include "../../drivers/apple-agx/shared/include/apple_agx_gpuva_broker_v5.h"
struct hv_agx_gpuva_v5_wire {
    AGX_GPUVA_V5_REQUEST request;
    AGX_GPUVA_V5_RESPONSE response;
    uint64_t last_sequence;
};
typedef void (*hv_agx_gpuva_v5_execute)(void *, const AGX_GPUVA_V5_REQUEST *,
                                        AGX_GPUVA_V5_RESPONSE *);
bool hv_agx_gpuva_v5_mmio(struct hv_agx_gpuva_v5_wire *, uint64_t offset,
                          uint64_t *value, bool write, unsigned width,
                          hv_agx_gpuva_v5_execute execute, void *context);
#endif
