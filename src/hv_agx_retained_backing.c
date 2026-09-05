#include "hv_agx_retained_backing.h"
static bool overlaps(uint64_t a,uint64_t n,uint64_t b,uint64_t m)
{
    return m && a < b+m && b < a+n;
}
bool hv_agx_retained_backing_allowed(const struct hv_contract_snapshot *s,
    uint64_t pa,uint64_t bytes,uint64_t ramdisk,uint64_t ramdisk_bytes)
{
    unsigned i;
    bool guest_region = false;
    if (!s || bytes != 0x4000 || (pa & 0x3fff) || pa > UINT64_MAX-bytes ||
        !s->region_count || s->region_count > HV_CONTRACT_MAX_REGIONS ||
        !s->boot.ram_size || s->boot.ram_base > UINT64_MAX-s->boot.ram_size ||
        pa < s->boot.ram_base || bytes > s->boot.ram_size ||
        pa-s->boot.ram_base > s->boot.ram_size-bytes ||
        ramdisk > UINT64_MAX-ramdisk_bytes || overlaps(pa,bytes,ramdisk,ramdisk_bytes))
        return false;
    for(i=0;i<s->region_count;++i) {
        const struct hv_contract_region *r=&s->regions[i];
        if (r->kind > HV_CONTRACT_REGION_DART_TABLES || r->base > UINT64_MAX-r->size)
            return false;
        if(r->kind==HV_CONTRACT_REGION_GUEST_RAM) {
            if(pa>=r->base && bytes<=r->size && pa-r->base<=r->size-bytes)
                guest_region=true;
        } else if(r->kind!=HV_CONTRACT_REGION_LOW_MEMORY && overlaps(pa,bytes,r->base,r->size))
            return false;
    }
    return guest_region;
}
