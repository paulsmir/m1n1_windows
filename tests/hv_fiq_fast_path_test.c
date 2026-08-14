#include <assert.h>
#include <stdio.h>

#include "../src/hv_fiq_fast_path.h"

static void test_secondary_services_local_sources_before_global_lock(void)
{
    assert(hv_fiq_secondary_fast_eligible(3, 0, -1, false));
    /* The live LR plus the freshly recomputed HCR.VI is the delivery state.
     * A Pending virtual IRQ is local work already completed by this vCPU and
     * is deliberately not an input to this completion policy: it must not
     * serialize every timer expiry through the global bhl. */
    assert(hv_fiq_secondary_fast_complete(true, false));
}

static void test_global_work_stays_on_serialized_path(void)
{
    assert(!hv_fiq_secondary_fast_eligible(0, 0, -1, false));
    assert(!hv_fiq_secondary_fast_eligible(3, 0, 2, false));
    assert(!hv_fiq_secondary_fast_complete(true, true));
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
