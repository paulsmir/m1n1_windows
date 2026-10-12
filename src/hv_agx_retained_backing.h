#ifndef HV_AGX_RETAINED_BACKING_H
#define HV_AGX_RETAINED_BACKING_H
#include "hv_launch_contract.h"
/* A 16 KiB GPU backing page is allowed only inside published guest RAM and
 * outside every other published region except the low-memory alias (M1: a
 * preloaded ramdisk is denied through its own region, not a fixed window). */
bool hv_agx_retained_backing_allowed(const struct hv_contract_snapshot *s,
    uint64_t pa,uint64_t bytes);
#endif
