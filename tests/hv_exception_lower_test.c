#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/hv_exception_lower.h"

static void expect_plan(uint64_t source_spsr, uint64_t vbar, uint64_t vector_offset)
{
    struct hv_exception_lower_plan plan;

    assert(hv_exception_lower_plan(source_spsr, vbar, &plan));
    assert(plan.target_elr == vbar + vector_offset);
    assert((plan.target_spsr & 0x1f) == 0x5);
    assert((plan.target_spsr & 0x3c0) == 0x3c0);
}

int main(void)
{
    const uint64_t vbar = 0xfffff80100002000ULL;
    struct hv_exception_lower_plan plan;

    expect_plan(0x0, vbar, 0x400); /* EL0t: lower EL, AArch64. */
    expect_plan(0x4, vbar, 0x000); /* EL1t: current EL with SP0. */
    expect_plan(0x5, vbar, 0x200); /* EL1h: current EL with SPx. */
    assert(!hv_exception_lower_plan(0x8, vbar, &plan));

    puts("hv_exception_lower_test: ok");
    return 0;
}
