#include <assert.h>
#include <stdio.h>

#include "../src/cpufreq_state.h"
#include "../src/soc.h"

int main(void)
{
    assert(cpufreq_pstate_supported(T8103));
    assert(!cpufreq_pstate_supported(0xdead));
    assert(cpufreq_decode_pstate(T8103, 0) == 0);
    assert(cpufreq_decode_pstate(T8103, 5) == 5);
    assert(cpufreq_decode_pstate(T8103, 7 | (1ULL << 25)) == 7);
    assert(cpufreq_decode_pstate(S5L8960X, 3ULL << 22) == 3);

    puts("cpufreq_state_test: ok");
    return 0;
}
