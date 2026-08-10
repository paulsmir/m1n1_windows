#include <assert.h>
#include <stdio.h>

#include "../src/hv_fiq_fast_path.h"

static void test_secondary_services_local_sources_before_global_lock(void)
{
    assert(hv_fiq_secondary_fast_eligible(3, 0, -1));
    assert(hv_fiq_secondary_fast_complete(true, false));
}

static void test_global_work_stays_on_serialized_path(void)
{
    assert(!hv_fiq_secondary_fast_eligible(0, 0, -1));
    assert(!hv_fiq_secondary_fast_eligible(3, 0, 2));
    assert(!hv_fiq_secondary_fast_complete(true, true));
}

int main(void)
{
    test_secondary_services_local_sources_before_global_lock();
    test_global_work_stays_on_serialized_path();
    puts("hv_fiq_fast_path_test: ok");
    return 0;
}
