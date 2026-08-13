#include <assert.h>
#include <stdio.h>

#include "../src/hv_timer_delivery.h"

static void test_repeated_assertion_has_one_owner(void)
{
    struct hv_timer_delivery_queue queue = {0};

    assert(hv_timer_delivery_assert(&queue, 18, 0x20));
    assert(!hv_timer_delivery_assert(&queue, 18, 0x20));
    assert(hv_timer_delivery_contains(&queue, 18));
}

static void test_deassertion_withdraws_stale_delivery(void)
{
    struct hv_timer_delivery_queue queue = {0};
    struct hv_timer_delivery out = {0};

    assert(hv_timer_delivery_assert(&queue, 18, 0x20));
    assert(hv_timer_delivery_deassert(&queue, 18));
    assert(!hv_timer_delivery_contains(&queue, 18));
    assert(!hv_timer_delivery_pop(&queue, &out));
}

static void test_two_timer_sources_keep_fifo_order(void)
{
    struct hv_timer_delivery_queue queue = {0};
    struct hv_timer_delivery out = {0};

    assert(hv_timer_delivery_assert(&queue, 17, 0x30));
    assert(hv_timer_delivery_assert(&queue, 18, 0x20));
    assert(hv_timer_delivery_pop(&queue, &out));
    assert(out.intid == 17);
    assert(out.priority == 0x30);
    assert(hv_timer_delivery_pop(&queue, &out));
    assert(out.intid == 18);
    assert(out.priority == 0x20);
    assert(!hv_timer_delivery_pop(&queue, &out));
}

static void test_reasserted_withdrawn_source_moves_to_the_back(void)
{
    struct hv_timer_delivery_queue queue = {0};
    struct hv_timer_delivery out = {0};

    assert(hv_timer_delivery_assert(&queue, 17, 0x30));
    assert(hv_timer_delivery_assert(&queue, 18, 0x20));
    assert(hv_timer_delivery_deassert(&queue, 17));
    assert(hv_timer_delivery_assert(&queue, 17, 0x10));
    assert(hv_timer_delivery_pop(&queue, &out) && out.intid == 18);
    assert(hv_timer_delivery_pop(&queue, &out) && out.intid == 17);
    assert(out.priority == 0x10);
}

static void test_rejects_non_timer_intids(void)
{
    struct hv_timer_delivery_queue queue = {0};

    assert(!hv_timer_delivery_assert(&queue, 16, 0x20));
    assert(!hv_timer_delivery_assert(&queue, 19, 0x20));
    assert(!hv_timer_delivery_deassert(&queue, 19));
    assert(!hv_timer_delivery_contains(&queue, 19));
}

int main(void)
{
    test_repeated_assertion_has_one_owner();
    test_deassertion_withdraws_stale_delivery();
    test_two_timer_sources_keep_fifo_order();
    test_reasserted_withdrawn_source_moves_to_the_back();
    test_rejects_non_timer_intids();
    puts("hv_timer_delivery_test: ok");
    return 0;
}
