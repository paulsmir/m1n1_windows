#include <assert.h>
#include <stddef.h>
#include "../src/hv_agx_retained_mmio.h"

static unsigned dispatched;
static void execute(void *context, const AGX_RR_REQUEST *request,
                    AGX_RR_RESPONSE *response)
{
    (void)context;
    assert(request->Command == AGX_RR_QUERY_TABLE_HASH);
    ++dispatched;
    response->Status = 0;
    response->Epoch = request->Epoch;
    response->Root = 0x9fff78000ULL;
    response->Pa = 0x123456789abcdef0ULL;
    response->Count = 4;
    hv_agx_retained_finalize_count(response, request->Command, 207);
}

int main(void)
{
    struct hv_agx_retained_mmio state = {0};
    AGX_RR_RESPONSE response = {0};
    unsigned long long doorbell = 1, count = 0;
    response.Count = 4;
    hv_agx_retained_finalize_count(&response, AGX_RR_QUERY_TABLE_HASH, 207);
    assert(response.Count == 4);
    hv_agx_retained_finalize_count(&response, AGX_RR_QUERY, 207);
    assert(response.Count == 207);
    state.Request.Version = AGX_RR_ABI_VERSION;
    state.Request.Bytes = sizeof(state.Request);
    state.Request.Command = AGX_RR_QUERY_TABLE_HASH;
    state.Request.Sequence = 1;
    state.Request.Epoch = 7;
    assert(hv_agx_retained_mmio(&state, AGX_RR_DOORBELL, &doorbell, 1, 2,
                                 execute, 0));
    assert(dispatched == 1 && state.Response.Status == 0);
    assert(hv_agx_retained_mmio(&state,
        AGX_RR_RESPONSE_OFFSET + offsetof(AGX_RR_RESPONSE, Count),
        &count, 0, 3, execute, 0));
    assert(count == 4 && state.Response.Pa == 0x123456789abcdef0ULL);
    return 0;
}
