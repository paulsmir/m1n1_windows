/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdio.h>

#include "../src/rtkit_deferred.h"

static void test_deferred_system_messages_are_preserved_fifo(void)
{
    struct rtkit_deferred_queue queue;
    struct rtkit_deferred_message first = {.ep = 1, .msg = 0x111};
    struct rtkit_deferred_message second = {.ep = 2, .msg = 0x222};
    struct rtkit_deferred_message out = {0};

    rtkit_deferred_init(&queue);
    assert(rtkit_deferred_push(&queue, &first));
    assert(rtkit_deferred_push(&queue, &second));
    assert(rtkit_deferred_pop(&queue, &out));
    assert(out.ep == first.ep && out.msg == first.msg);
    assert(rtkit_deferred_pop(&queue, &out));
    assert(out.ep == second.ep && out.msg == second.msg);
    assert(!rtkit_deferred_pop(&queue, &out));
}

static void test_full_queue_rejects_without_overwriting(void)
{
    struct rtkit_deferred_queue queue;
    struct rtkit_deferred_message msg = {0};
    struct rtkit_deferred_message out = {0};

    rtkit_deferred_init(&queue);
    for (unsigned int i = 0; i < RTKIT_DEFERRED_CAPACITY; ++i) {
        msg.msg = i + 1;
        assert(rtkit_deferred_push(&queue, &msg));
    }
    msg.msg = 0xdead;
    assert(!rtkit_deferred_push(&queue, &msg));
    for (unsigned int i = 0; i < RTKIT_DEFERRED_CAPACITY; ++i) {
        assert(rtkit_deferred_pop(&queue, &out));
        assert(out.msg == i + 1);
    }
}

int main(void)
{
    test_deferred_system_messages_are_preserved_fifo();
    test_full_queue_rejects_without_overwriting();
    puts("rtkit_deferred_test: ok");
    return 0;
}
