#ifndef HV_AGX_FIRMWARE_PREFIX_H
#define HV_AGX_FIRMWARE_PREFIX_H
#include "../../drivers/apple-agx/shared/include/apple_agx_firmware_prefix.h"
typedef unsigned char (*hv_agx_prefix_reader)(void *, unsigned long long,
                                            unsigned long long [2]);
unsigned char hv_agx_firmware_prefix_read(
    unsigned long long base, unsigned long long length, unsigned long long epoch,
    hv_agx_prefix_reader reader, void *context, unsigned long long offset,
    unsigned long long *value, unsigned char write, unsigned int width);
#endif
