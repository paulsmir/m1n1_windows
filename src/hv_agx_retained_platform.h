#ifndef HV_AGX_RETAINED_PLATFORM_H
#define HV_AGX_RETAINED_PLATFORM_H
#include "types.h"
bool hv_agx_retained_platform_init(u64 root, u64 length);
/* M2-f6: the GPU broker's backing table for system_pages guest pages,
 * allocated at hypervisor start (NULL keeps the built-in 2 GiB table). */
void hv_agx_retained_gpuva_storage(void *storage, unsigned system_pages);
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
u64 hv_agx_retained_platform_ram_page(u64 ipa);
bool hv_agx_retained_platform_page_busy(u64 pa);
#endif
