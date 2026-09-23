#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../src/hv_agx_gpuva_v5_mmio.h"

static unsigned executed;
static void execute(void *opaque, const AGX_GPUVA_V5_REQUEST *q,
                    AGX_GPUVA_V5_RESPONSE *r)
{
    (void)opaque;
    assert(q->ProcessId == 17 && q->Command == AGX_GPUVA_V5_CREATE);
    ++executed;
    r->Status = 0;
    r->Epoch = q->Epoch;
}
static void write64(struct hv_agx_gpuva_v5_wire *s, unsigned offset, uint64_t value)
{
    assert(hv_agx_gpuva_v5_mmio(s,offset,&value,true,3,execute,NULL));
}
static void write32(struct hv_agx_gpuva_v5_wire *s, unsigned offset, uint32_t value)
{
    uint64_t wire=value;
    assert(hv_agx_gpuva_v5_mmio(s,offset,&wire,true,2,execute,NULL));
}
int main(void)
{
    struct hv_agx_gpuva_v5_wire s={0};
    AGX_GPUVA_V5_REQUEST q={0};
    uint64_t read=0, one=1;
    q.Version=5; q.Bytes=sizeof(q); q.Command=AGX_GPUVA_V5_CREATE;
    q.Sequence=1; q.Epoch=7; q.ProcessId=17;
    for (unsigned i=0;i<sizeof(q);i+=8) {
        uint64_t word;
        memcpy(&word,(unsigned char *)&q+i,8);
        write64(&s,i,word);
    }
    write32(&s,AGX_GPUVA_V5_DOORBELL,1);
    assert(executed==1 && s.response.Receipt==1 && s.response.Epoch==7);
    assert(hv_agx_gpuva_v5_mmio(&s,AGX_GPUVA_V5_RESPONSE_OFFSET,&read,false,3,
                                execute,NULL));
    assert(read==1);
    write32(&s,AGX_GPUVA_V5_DOORBELL,1);
    assert(executed==1 && s.response.Status!=0);
    assert(!hv_agx_gpuva_v5_mmio(&s,AGX_GPUVA_V5_DOORBELL,&one,true,3,
                                 execute,NULL));
    assert(!hv_agx_gpuva_v5_mmio(&s,0x100,&read,false,3,execute,NULL));
    return 0;
}
