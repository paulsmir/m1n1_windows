#include <assert.h>
#include "../src/hv_agx_retained_mmio.h"

int main(void)
{
    AGX_RR_RESPONSE response = {0};
    response.Count = 4;
    hv_agx_retained_finalize_count(&response, AGX_RR_QUERY_TABLE_HASH, 207);
    assert(response.Count == 4);
    hv_agx_retained_finalize_count(&response, AGX_RR_QUERY, 207);
    assert(response.Count == 207);
    return 0;
}
