/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/afk_deferred_message.h"

int main(void)
{
    unsigned char storage[16] = {0};
    const unsigned char payload[] = {1, 2, 3, 4};
    struct afk_deferred_message slot;
    struct afk_deferred_view view;

    afk_deferred_message_init(&slot, storage, sizeof(storage));
    assert(afk_deferred_message_store(&slot, 7, 0, payload, sizeof(payload)));
    assert(!afk_deferred_message_store(&slot, 8, 4, payload, sizeof(payload)));
    assert(afk_deferred_message_take(&slot, &view));
    assert(view.channel == 7 && view.type == 0 && view.size == sizeof(payload));
    assert(memcmp(view.data, payload, sizeof(payload)) == 0);
    afk_deferred_message_release(&slot);
    assert(!afk_deferred_message_take(&slot, &view));
    assert(!afk_deferred_message_store(&slot, 1, 0, payload, sizeof(storage) + 1));
    puts("afk_deferred_message_test: ok");
    return 0;
}
