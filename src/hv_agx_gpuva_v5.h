#ifndef HV_AGX_GPUVA_V5_H
#define HV_AGX_GPUVA_V5_H

#include <stdint.h>
#include <stdbool.h>

#define HV_AGX_GPUVA_V5_VERSION 5u
#define HV_AGX_GPUVA_V5_PAGE UINT64_C(0x4000)
#define HV_AGX_GPUVA_V5_PROCESSES 128u
#define HV_AGX_GPUVA_V5_TABLES 512u
#define HV_AGX_GPUVA_V5_BACKINGS 1024u
#define HV_AGX_GPUVA_V5_SLOTS 64u

enum hv_agx_gpuva_v5_result {
    HV_AGX_GPUVA_V5_OK = 0,
    HV_AGX_GPUVA_V5_INVALID,
    HV_AGX_GPUVA_V5_STALE,
    HV_AGX_GPUVA_V5_BUSY,
    HV_AGX_GPUVA_V5_OWNERSHIP,
    HV_AGX_GPUVA_V5_TLB,
    HV_AGX_GPUVA_V5_TAINTED,
    HV_AGX_GPUVA_V5_CAPACITY,
};

struct hv_agx_gpuva_v5_ops {
    void *context;
    /* Full aligned 16-KiB guest normal RAM page, already screened from
     * firmware/private regions. Zero means forbidden. */
    uint64_t (*translate_page)(void *, uint64_t ipa);
    uint64_t *(*map_page)(void *, uint64_t ipa, uint64_t pa);
    bool (*read_slot)(void *, unsigned slot, uint64_t *ttbr0, uint64_t *ttbr1);
    bool (*write_slot)(void *, unsigned slot, uint64_t ttbr0, uint64_t ttbr1);
    bool (*sync_tables)(void *);
    /* DSB store visibility + ASID TLBI + DSB/ISB acknowledgement. */
    bool (*invalidate)(void *, unsigned slot);
    bool (*prefix_unchanged)(void *);
    /* v4 uses fixed context63 until its owner is explicitly disabled. */
    bool (*legacy_slot63_active)(void *);
};

struct hv_agx_gpuva_v5_process {
    uint64_t identity, generation, root_pa, root_generation, map_generation;
    bool live, paging;
};
struct hv_agx_gpuva_v5_table {
    uint64_t ipa, pa;
    uint64_t *entries;
    unsigned owner, level;
    bool live;
};
struct hv_agx_gpuva_v5_slot {
    uint64_t token;
    unsigned owner, jobs;
    bool occupied;
};
struct hv_agx_gpuva_v5_backing {
    uint64_t ipa, pa, generation;
    unsigned owner;
    bool live;
    bool shared;
};
struct hv_agx_gpuva_v5 {
    struct hv_agx_gpuva_v5_ops ops;
    uint64_t epoch, context0_ttbr0, context0_ttbr1, next_token;
    struct hv_agx_gpuva_v5_process processes[HV_AGX_GPUVA_V5_PROCESSES];
    struct hv_agx_gpuva_v5_table tables[HV_AGX_GPUVA_V5_TABLES];
    struct hv_agx_gpuva_v5_backing backings[HV_AGX_GPUVA_V5_BACKINGS];
    struct hv_agx_gpuva_v5_slot slots[HV_AGX_GPUVA_V5_SLOTS];
    bool active, tainted;
};

enum hv_agx_gpuva_v5_result hv_agx_gpuva_v5_init(
    struct hv_agx_gpuva_v5 *, uint64_t epoch, const struct hv_agx_gpuva_v5_ops *);
enum hv_agx_gpuva_v5_result hv_agx_gpuva_v5_verify(struct hv_agx_gpuva_v5 *);
enum hv_agx_gpuva_v5_result hv_agx_gpuva_v5_validate_envelope(
    const struct hv_agx_gpuva_v5 *, uint64_t epoch, unsigned command,
    unsigned flags);
enum hv_agx_gpuva_v5_result hv_agx_gpuva_v5_create(
    struct hv_agx_gpuva_v5 *, uint64_t id, uint64_t generation, uint64_t root_ipa,
    bool paging);
enum hv_agx_gpuva_v5_result hv_agx_gpuva_v5_register_table(
    struct hv_agx_gpuva_v5 *, uint64_t id, uint64_t generation,
    uint64_t table_ipa, unsigned level);
enum hv_agx_gpuva_v5_result hv_agx_gpuva_v5_register_backing(
    struct hv_agx_gpuva_v5 *, uint64_t id, uint64_t generation,
    uint64_t allocation_generation, uint64_t page_ipa);
enum hv_agx_gpuva_v5_result hv_agx_gpuva_v5_register_shared_backing(
    struct hv_agx_gpuva_v5 *, uint64_t id, uint64_t generation,
    uint64_t allocation_generation, uint64_t page_ipa);
enum hv_agx_gpuva_v5_result hv_agx_gpuva_v5_revoke_backing(
    struct hv_agx_gpuva_v5 *, uint64_t id, uint64_t generation,
    uint64_t allocation_generation, uint64_t page_ipa);
enum hv_agx_gpuva_v5_result hv_agx_gpuva_v5_revoke_table(
    struct hv_agx_gpuva_v5 *, uint64_t id, uint64_t generation,
    uint64_t table_ipa, unsigned level);
enum hv_agx_gpuva_v5_result hv_agx_gpuva_v5_update_parent(
    struct hv_agx_gpuva_v5 *, uint64_t id, uint64_t generation,
    uint64_t table_ipa, unsigned index, uint64_t child_ipa);
enum hv_agx_gpuva_v5_result hv_agx_gpuva_v5_update_leaf(
    struct hv_agx_gpuva_v5 *, uint64_t id, uint64_t generation,
    uint64_t table_ipa, unsigned index, const uint64_t logical_ipa[4],
    uint64_t allocation_generation, unsigned update_mask, unsigned valid_mask,
    unsigned writable_mask);
enum hv_agx_gpuva_v5_result hv_agx_gpuva_v5_relocate_root(
    struct hv_agx_gpuva_v5 *, uint64_t id, uint64_t generation,
    uint64_t root_ipa);
enum hv_agx_gpuva_v5_result hv_agx_gpuva_v5_lease(
    struct hv_agx_gpuva_v5 *, uint64_t id, uint64_t generation,
    unsigned slot, uint64_t *token);
enum hv_agx_gpuva_v5_result hv_agx_gpuva_v5_job_begin(
    struct hv_agx_gpuva_v5 *, unsigned slot, uint64_t token);
enum hv_agx_gpuva_v5_result hv_agx_gpuva_v5_job_end(
    struct hv_agx_gpuva_v5 *, unsigned slot, uint64_t token);
enum hv_agx_gpuva_v5_result hv_agx_gpuva_v5_release(
    struct hv_agx_gpuva_v5 *, unsigned slot, uint64_t token);
enum hv_agx_gpuva_v5_result hv_agx_gpuva_v5_destroy(
    struct hv_agx_gpuva_v5 *, uint64_t id, uint64_t generation);

#endif
