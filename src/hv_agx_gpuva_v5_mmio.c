#include "hv_agx_gpuva_v5_mmio.h"
#include <string.h>
_Static_assert(sizeof(AGX_GPUVA_V5_REQUEST)==128, "v5 request ABI");
_Static_assert(sizeof(AGX_GPUVA_V5_RESPONSE)==64, "v5 response ABI");
bool hv_agx_gpuva_v5_mmio(struct hv_agx_gpuva_v5_wire *s, uint64_t offset,
    uint64_t *value, bool write, unsigned width, hv_agx_gpuva_v5_execute execute,
    void *context)
{
    unsigned bytes;
    if (!s || !value || width > 3) return false;
    bytes = 1u << width;
    if (offset & (bytes - 1)) return false;
    if (write && offset == AGX_GPUVA_V5_DOORBELL) {
        AGX_GPUVA_V5_REQUEST q = s->request;
        AGX_GPUVA_V5_RESPONSE r = {0};
        if (width != 2 || *value != 1) return false;
        r.Receipt = q.Sequence;
        r.Status = 1;
        if (q.Version == AGX_GPUVA_V5_VERSION && q.Bytes == sizeof(q) &&
            q.Sequence && q.Sequence > s->last_sequence &&
            q.Command >= AGX_GPUVA_V5_CREATE && q.Command <= AGX_GPUVA_V5_REVOKE_TABLE &&
            execute) {
            s->last_sequence = q.Sequence;
            execute(context, &q, &r);
        }
        s->response = r;
        return true;
    }
    if (offset <= sizeof(s->request) - bytes) {
        if (write) memcpy((unsigned char *)&s->request + offset, value, bytes);
        else { *value = 0; memcpy(value, (unsigned char *)&s->request + offset, bytes); }
        return true;
    }
    if (!write && offset >= AGX_GPUVA_V5_RESPONSE_OFFSET &&
        offset - AGX_GPUVA_V5_RESPONSE_OFFSET <= sizeof(s->response) - bytes) {
        *value = 0;
        memcpy(value, (unsigned char *)&s->response + offset - AGX_GPUVA_V5_RESPONSE_OFFSET,
               bytes);
        return true;
    }
    return false;
}
