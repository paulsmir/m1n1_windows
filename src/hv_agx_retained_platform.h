#ifndef HV_AGX_RETAINED_PLATFORM_H
#define HV_AGX_RETAINED_PLATFORM_H
#include "types.h"
bool hv_agx_retained_platform_init(u64 root, u64 length);
bool hv_agx_retained_platform_mmio(u64 offset, u64 *value, bool write,
                                  unsigned width, bool powered);
bool hv_agx_retained_platform_gpuva_v5(u64 offset, u64 *value, bool write,
                                      unsigned width, bool powered);
bool hv_agx_retained_platform_io(u64 offset,u64 *value,bool write,
                                 unsigned width,bool powered);
bool hv_agx_retained_platform_profile(u64 offset,u64 *value,bool write,
                                      unsigned width,bool powered);
bool hv_agx_retained_gpu_region(struct exc_info *ctx, u64 addr, u64 *value,
                                bool write, int width);
bool hv_agx_retained_can_power_off(void);
#endif
