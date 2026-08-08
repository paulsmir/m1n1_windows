#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "../src/hv_sgi_pending.h"

#define TEST_SGI_BIT (1u << 3)

struct ipi_state {
    u32 pending_mask;
    bool hardware_pending;
};

static void queue_sgi(struct ipi_state *state)
{
    u32 old = __atomic_fetch_or(&state->pending_mask, TEST_SGI_BIT, __ATOMIC_RELEASE);
    if (!(old & TEST_SGI_BIT))
        state->hardware_pending = true;
}

static void acknowledge_with_racing_sender(void *opaque)
{
    struct ipi_state *state = opaque;

    /* The sender wins immediately before the receiver acknowledges the old IPI. */
    queue_sgi(state);
    state->hardware_pending = false;
}

static void test_sender_before_ack_cannot_leave_pending_sgi_without_wakeup(void)
{
    struct ipi_state state = {
        .pending_mask = TEST_SGI_BIT,
        .hardware_pending = true,
    };

    u32 taken =
        hv_sgi_ack_and_take_pending(&state.pending_mask, acknowledge_with_racing_sender, &state);

    assert(taken == TEST_SGI_BIT);
    assert(state.pending_mask == 0);
    assert(!state.hardware_pending);
}

int main(void)
{
    test_sender_before_ack_cannot_leave_pending_sgi_without_wakeup();
    puts("hv_sgi_pending_test: ok");
    return 0;
}
