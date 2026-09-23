#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../src/hv_agx_gpuva_v5.h"

#define P_ROOT UINT64_C(0x10000000)
#define P_L1   UINT64_C(0x10004000)
#define P_L2   UINT64_C(0x10008000)
#define P_DATA UINT64_C(0x20000000)
#define P_ROOT2 UINT64_C(0x1000c000)
#define Q_ROOT UINT64_C(0x11000000)
#define Q_L1   UINT64_C(0x11004000)
#define Q_L2   UINT64_C(0x11008000)
#define Q_DATA UINT64_C(0x30000000)

struct fixture {
    struct hv_agx_gpuva_v5 broker;
    uint64_t pages[9][2048];
    uint64_t ipa[9];
    uint64_t slots[64][2];
    unsigned invalidations[64], fail_once;
    bool prefix;
    bool legacy_slot63;
};
static uint64_t translate(void *opaque, uint64_t ipa)
{
    struct fixture *f = opaque;
    unsigned i;
    for (i = 0; i < 9; ++i) if (f->ipa[i] == ipa) return ipa;
    return 0;
}
static uint64_t *map_page(void *opaque, uint64_t ipa, uint64_t pa)
{
    struct fixture *f = opaque;
    unsigned i;
    if (ipa != pa) return NULL;
    for (i = 0; i < 9; ++i) if (f->ipa[i] == ipa) return f->pages[i];
    return NULL;
}
static bool read_slot(void *opaque, unsigned slot, uint64_t *low, uint64_t *high)
{
    struct fixture *f = opaque;
    if (slot >= 64) return false;
    *low = f->slots[slot][0]; *high = f->slots[slot][1]; return true;
}
static bool write_slot(void *opaque, unsigned slot, uint64_t low, uint64_t high)
{
    struct fixture *f = opaque;
    if (!slot || slot >= 64 || high) return false;
    f->slots[slot][0] = low; f->slots[slot][1] = high; return true;
}
static bool sync_tables(void *opaque) { (void)opaque; return true; }
static bool invalidate(void *opaque, unsigned slot)
{
    struct fixture *f = opaque;
    if (!slot || slot >= 64) return false;
    if (f->fail_once) { --f->fail_once; return false; }
    ++f->invalidations[slot]; return true;
}
static bool prefix(void *opaque) { return ((struct fixture *)opaque)->prefix; }
static bool legacy_slot63(void *opaque)
{ return ((struct fixture *)opaque)->legacy_slot63; }
static struct fixture *new_fixture(void)
{
    struct fixture *f = calloc(1, sizeof(*f));
    struct hv_agx_gpuva_v5_ops ops;
    assert(f);
    f->ipa[0]=P_ROOT; f->ipa[1]=P_L1; f->ipa[2]=P_L2;
    f->ipa[3]=P_DATA; f->ipa[4]=Q_ROOT; f->ipa[5]=Q_L1;
    f->ipa[6]=Q_L2; f->ipa[7]=Q_DATA; f->ipa[8]=P_ROOT2;
    f->slots[0][0] = UINT64_C(0x91000001);
    f->slots[0][1] = UINT64_C(0x90000001);
    f->prefix = true;
    ops = (struct hv_agx_gpuva_v5_ops){f,translate,map_page,read_slot,
                                      write_slot,sync_tables,invalidate,prefix,
                                      legacy_slot63};
    assert(hv_agx_gpuva_v5_init(&f->broker, 7, &ops) == HV_AGX_GPUVA_V5_OK);
    return f;
}
static void prepare(struct fixture *f, uint64_t id, uint64_t root,
                    uint64_t l1, uint64_t l2, uint64_t data, bool paging)
{
    uint64_t logical[4] = {data,data+0x1000,data+0x2000,data+0x3000};
    assert(hv_agx_gpuva_v5_create(&f->broker,id,1,root,paging)==HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_register_table(&f->broker,id,1,l1,1)==HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_register_table(&f->broker,id,1,l2,2)==HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_register_backing(&f->broker,id,1,17,data)==HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_update_parent(&f->broker,id,1,root,0,l1)==HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_update_parent(&f->broker,id,1,l1,0,l2)==HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_update_leaf(&f->broker,id,1,l2,8,logical,17,15,15,15)==
           HV_AGX_GPUVA_V5_OK);
}
static void cleanup(struct fixture *f, uint64_t id, uint64_t root,
                    uint64_t l1, uint64_t l2, uint64_t data)
{
    assert(hv_agx_gpuva_v5_revoke_backing(&f->broker,id,1,17,data)==
           HV_AGX_GPUVA_V5_BUSY);
    assert(hv_agx_gpuva_v5_update_leaf(&f->broker,id,1,l2,8,NULL,0,15,0,0)==
           HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_revoke_backing(&f->broker,id,1,17,data)==
           HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_revoke_table(&f->broker,id,1,l2,2)==
           HV_AGX_GPUVA_V5_BUSY);
    assert(hv_agx_gpuva_v5_update_parent(&f->broker,id,1,l1,0,0)==
           HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_revoke_table(&f->broker,id,1,l2,2)==
           HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_update_parent(&f->broker,id,1,root,0,0)==
           HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_revoke_table(&f->broker,id,1,l1,1)==
           HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_destroy(&f->broker,id,1)==HV_AGX_GPUVA_V5_OK);
}
int main(void)
{
    struct fixture *f = new_fixture();
    uint64_t ptoken, qtoken, logical[4]={P_DATA,P_DATA+0x1000,
                                          P_DATA+0x2000,P_DATA+0x3000};
    uint64_t original, ctx0lo=f->slots[0][0], ctx0hi=f->slots[0][1];
    prepare(f,1,P_ROOT,P_L1,P_L2,P_DATA,true);
    prepare(f,2,Q_ROOT,Q_L1,Q_L2,Q_DATA,false);
    assert(f->pages[2][8] != f->pages[6][8]);
    original=f->pages[2][8];
    assert(hv_agx_gpuva_v5_update_leaf(&f->broker,1,1,P_L2,8,logical,17,2,2,2)==
           HV_AGX_GPUVA_V5_OK);
    assert(f->pages[2][8]==original);
    assert(hv_agx_gpuva_v5_update_leaf(&f->broker,1,1,P_L2,8,NULL,0,2,0,0)==
           HV_AGX_GPUVA_V5_INVALID);
    assert(f->pages[2][8]==original);
    assert(hv_agx_gpuva_v5_update_leaf(&f->broker,1,1,P_L2,9,logical,18,15,15,15)==
           HV_AGX_GPUVA_V5_OWNERSHIP);
    logical[1]=Q_DATA+0x1000;
    assert(hv_agx_gpuva_v5_update_leaf(&f->broker,1,1,P_L2,9,logical,17,15,15,15)==
           HV_AGX_GPUVA_V5_OWNERSHIP);
    assert(f->pages[2][9]==0);
    assert(hv_agx_gpuva_v5_lease(&f->broker,1,1,0,&ptoken)==HV_AGX_GPUVA_V5_INVALID);
    assert(hv_agx_gpuva_v5_lease(&f->broker,1,1,1,&ptoken)==HV_AGX_GPUVA_V5_OK);
    assert(f->slots[1][1]==0);
    assert(hv_agx_gpuva_v5_job_begin(&f->broker,1,ptoken)==HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_register_table(&f->broker,1,1,P_ROOT2,0)==
           HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_relocate_root(&f->broker,1,1,P_ROOT2)==
           HV_AGX_GPUVA_V5_BUSY);
    assert(hv_agx_gpuva_v5_release(&f->broker,1,ptoken)==HV_AGX_GPUVA_V5_BUSY);
    assert(hv_agx_gpuva_v5_lease(&f->broker,2,1,1,&qtoken)==HV_AGX_GPUVA_V5_BUSY);
    assert(hv_agx_gpuva_v5_job_end(&f->broker,1,ptoken)==HV_AGX_GPUVA_V5_OK);
    original=f->pages[2][8]; f->fail_once=1;
    assert(hv_agx_gpuva_v5_update_leaf(&f->broker,1,1,P_L2,8,NULL,0,15,0,0)==
           HV_AGX_GPUVA_V5_TLB);
    assert(f->pages[2][8]==original && !f->broker.tainted);
    assert(hv_agx_gpuva_v5_release(&f->broker,1,ptoken)==HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_relocate_root(&f->broker,1,1,P_ROOT2)==
           HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_revoke_table(&f->broker,1,1,P_ROOT2,0)==
           HV_AGX_GPUVA_V5_BUSY);
    assert(hv_agx_gpuva_v5_lease(&f->broker,2,1,1,&qtoken)==HV_AGX_GPUVA_V5_OK);
    assert(qtoken!=ptoken && f->slots[1][1]==0);
    assert(hv_agx_gpuva_v5_job_begin(&f->broker,1,ptoken)==HV_AGX_GPUVA_V5_STALE);
    assert(hv_agx_gpuva_v5_release(&f->broker,1,qtoken)==HV_AGX_GPUVA_V5_OK);
    f->legacy_slot63=true;
    assert(hv_agx_gpuva_v5_lease(&f->broker,1,1,63,&ptoken)==
           HV_AGX_GPUVA_V5_BUSY);
    f->legacy_slot63=false;
    assert(hv_agx_gpuva_v5_lease(&f->broker,1,1,63,&ptoken)==
           HV_AGX_GPUVA_V5_OK);
    assert(f->slots[63][1]==0);
    assert(hv_agx_gpuva_v5_release(&f->broker,63,ptoken)==HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_destroy(&f->broker,1,1)==HV_AGX_GPUVA_V5_BUSY);
    cleanup(f,1,P_ROOT,P_L1,P_L2,P_DATA);
    assert(hv_agx_gpuva_v5_revoke_table(&f->broker,1,1,P_ROOT2,0)==
           HV_AGX_GPUVA_V5_STALE);
    cleanup(f,2,Q_ROOT,Q_L1,Q_L2,Q_DATA);
    assert(f->slots[0][0]==ctx0lo && f->slots[0][1]==ctx0hi && f->prefix);
    free(f);
    f=new_fixture();
    f->slots[0][1] ^= 1;
    assert(hv_agx_gpuva_v5_create(&f->broker,1,1,P_ROOT,true)==
           HV_AGX_GPUVA_V5_TAINTED);
    free(f);
    f=new_fixture();
    prepare(f,1,P_ROOT,P_L1,P_L2,P_DATA,true);
    f->fail_once=2;
    assert(hv_agx_gpuva_v5_update_leaf(&f->broker,1,1,P_L2,8,NULL,0,15,0,0)==
           HV_AGX_GPUVA_V5_OK); /* No active slot, no TLBI dependency. */
    assert(hv_agx_gpuva_v5_lease(&f->broker,1,1,1,&ptoken)==
           HV_AGX_GPUVA_V5_TAINTED); /* Publish and rollback both lack ack. */
    free(f);
    f=new_fixture();
    f->legacy_slot63=true;
    assert(hv_agx_gpuva_v5_create(&f->broker,1,1,P_ROOT,true)==
           HV_AGX_GPUVA_V5_OK);
    uint64_t tokens[64]={0};
    for (unsigned slot=1;slot<=62;++slot)
        assert(hv_agx_gpuva_v5_lease(&f->broker,1,1,slot,&tokens[slot])==
               HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_lease(&f->broker,1,1,63,&tokens[63])==
           HV_AGX_GPUVA_V5_BUSY);
    for (unsigned slot=1;slot<=62;++slot)
        assert(hv_agx_gpuva_v5_release(&f->broker,slot,tokens[slot])==
               HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_destroy(&f->broker,1,1)==HV_AGX_GPUVA_V5_OK);
    free(f);
    return 0;
}
