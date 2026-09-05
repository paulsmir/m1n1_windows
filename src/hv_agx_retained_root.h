#ifndef HV_AGX_RETAINED_ROOT_H
#define HV_AGX_RETAINED_ROOT_H

#include "../../drivers/apple-agx/shared/include/apple_agx_uat_table.h"

#define HV_AGX_RETAINED_MAX_MAPPINGS 256u
#define HV_AGX_RETAINED_MAX_PAGES 24u
#define HV_AGX_RETAINED_WINDOWS_VA 0xffffffa000000000ULL
#define HV_AGX_RETAINED_WINDOWS_END 0xffffffa020000000ULL
#define HV_AGX_RETAINED_SYSTEM_VA 0xffffffa080000000ULL

enum hv_agx_retained_result {
    HV_AGX_RETAINED_OK = 0,
    HV_AGX_RETAINED_INVALID,
    HV_AGX_RETAINED_STATE,
    HV_AGX_RETAINED_RANGE,
    HV_AGX_RETAINED_OWNERSHIP,
    HV_AGX_RETAINED_ALLOCATION,
    HV_AGX_RETAINED_TAINTED,
};

struct hv_agx_retained_ops {
    void *Context;
    unsigned char (*AllocatePage)(void *, APPLE_AGX_UAT_PAGE *);
    void (*ReleasePage)(void *, const APPLE_AGX_UAT_PAGE *);
    /* Return zero unless the entire 16-KiB IPA page is guest normal RAM. */
    unsigned long long (*TranslateGuest)(void *, unsigned long long);
    /* Publish table changes and invalidate translations before reuse/free. */
    void (*Sync)(void *);
};

struct hv_agx_retained_mapping {
    unsigned long long Handle, Va, Ipa, Pa, Length;
};

/* EL2 internal state, never a guest wire object. Zero-initialize once and keep
 * at a stable address. Calls are serialized by the platform adapter. */
struct hv_agx_retained_root {
    APPLE_AGX_UAT_ROOTS Roots;
    unsigned long long Epoch, SystemVa, SystemBytes;
    unsigned int MappingCount;
    unsigned char Active, Prepared, PrefixUnchanged;
    unsigned char Tainted, PrefixSaved;
    unsigned long long RetainedPa, RegionBytes, NextHandle;
    unsigned long long *RetainedEntries;
    unsigned long long PrivatePrefix[2];
    struct hv_agx_retained_ops Ops;
    APPLE_AGX_UAT_ALLOCATOR Allocator;
    APPLE_AGX_UAT_INVENTORY Inventory;
    APPLE_AGX_UAT_PAGE Pages[HV_AGX_RETAINED_MAX_PAGES];
    APPLE_AGX_UAT_MAPPING UatMappings[HV_AGX_RETAINED_MAX_MAPPINGS + 1u];
    struct hv_agx_retained_mapping Mappings[HV_AGX_RETAINED_MAX_MAPPINGS];
    /* Exact successful UNMAP receipt, valid only in the current active epoch. */
    struct hv_agx_retained_mapping LastUnmap;
    APPLE_AGX_UAT_PAGE SystemPage;
};

int hv_agx_retained_prepare(struct hv_agx_retained_root *, unsigned long long,
                           unsigned long long *, unsigned long long,
                           unsigned long long, const struct hv_agx_retained_ops *);
int hv_agx_retained_activate(struct hv_agx_retained_root *);
int hv_agx_retained_map(struct hv_agx_retained_root *, unsigned long long,
                       unsigned long long, unsigned long long,
                       unsigned long long, unsigned long long *);
int hv_agx_retained_query(struct hv_agx_retained_root *, unsigned long long,
                         unsigned long long, unsigned long long,
                         unsigned long long, unsigned long long,
                         unsigned long long *);
int hv_agx_retained_unmap(struct hv_agx_retained_root *, unsigned long long,
                         unsigned long long, unsigned long long,
                         unsigned long long, unsigned long long);
int hv_agx_retained_verify_absent(struct hv_agx_retained_root *, unsigned long long,
                                 unsigned long long, unsigned long long,
                                 unsigned long long, unsigned long long);
int hv_agx_retained_close(struct hv_agx_retained_root *, unsigned long long,
                         unsigned char);
unsigned char hv_agx_retained_prefix_unchanged(struct hv_agx_retained_root *);

#endif
