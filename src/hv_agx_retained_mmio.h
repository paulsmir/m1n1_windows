#ifndef HV_AGX_RETAINED_MMIO_H
#define HV_AGX_RETAINED_MMIO_H
#include "../../drivers/apple-agx/shared/include/apple_agx_retained_root_abi.h"
struct hv_agx_retained_mmio {
  AGX_RR_REQUEST Request;
  AGX_RR_RESPONSE Response;
  unsigned long long LastSequence;
};
typedef void (*hv_agx_retained_execute)(void *, const AGX_RR_REQUEST *, AGX_RR_RESPONSE *);
void hv_agx_retained_finalize_count(AGX_RR_RESPONSE *response,
                                    unsigned command, unsigned mapping_count);
unsigned char hv_agx_retained_mmio(struct hv_agx_retained_mmio *state,
    unsigned long long offset, unsigned long long *value, unsigned char write,
    unsigned width, hv_agx_retained_execute execute, void *context);
#endif
