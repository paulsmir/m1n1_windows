#include <assert.h>
#include <stdio.h>

#include "../src/hv_fiq_fast_path.h"

static void test_secondary_services_local_sources_before_global_lock(void)
{
    assert(hv_fiq_secondary_fast_eligible(3, 0, -1, false));
    assert(hv_fiq_secondary_fast_complete(true, false, false));
}

static void test_global_work_stays_on_serialized_path(void)
{
    assert(!hv_fiq_secondary_fast_eligible(0, 0, -1, false));
    assert(!hv_fiq_secondary_fast_eligible(3, 0, 2, false));
    assert(!hv_fiq_secondary_fast_complete(true, true, false));
    /* The accepted path does not return to an idle guest while HCR.VI says a
     * virtual clock interrupt still needs to be observed. */
    assert(!hv_fiq_secondary_fast_complete(true, false, true));
}

static void test_host_rendezvous_forces_the_serialized_exit_path(void)
{
    /*
     * The host rendezvous sends a physical IPI and waits for hv_exc_entry() to
     * clear this CPU's hv_cpus_in_guest bit.  Consuming that IPI in the local
     * fast path would leave the bit set forever and report missing CPU 0xfe.
     */
    assert(!hv_fiq_secondary_fast_eligible(3, 0, -1, true));
}

int main(void)
{
    test_secondary_services_local_sources_before_global_lock();
    test_global_work_stays_on_serialized_path();
    test_host_rendezvous_forces_the_serialized_exit_path();
    puts("hv_fiq_fast_path_test: ok");
    return 0;
}
