#ifndef HV_AGX_RETAINED_BACKING_H
#define HV_AGX_RETAINED_BACKING_H
#include "hv_launch_contract.h"
bool hv_agx_retained_backing_allowed(const struct hv_contract_snapshot *s,
    uint64_t pa,uint64_t bytes,uint64_t ramdisk,uint64_t ramdisk_bytes);
#endif
