/* SPDX-License-Identifier: MIT */

#include "afk_deferred_message.h"
#include "string.h"

void afk_deferred_message_init(struct afk_deferred_message *slot, void *storage,
                               size_t capacity)
{
    if (!slot)
        return;
    memset(slot, 0, sizeof(*slot));
    slot->storage = storage;
    slot->capacity = capacity;
}

bool afk_deferred_message_store(struct afk_deferred_message *slot, uint32_t channel,
                                uint32_t type, const void *data, size_t size)
{
    if (!slot || slot->valid || (size && !data) || size > slot->capacity)
        return false;
    if (size)
        memcpy(slot->storage, data, size);
    slot->channel = channel;
    slot->type = type;
    slot->size = size;
    slot->valid = true;
    return true;
}

bool afk_deferred_message_take(const struct afk_deferred_message *slot,
                               struct afk_deferred_view *view)
{
    if (!slot || !view || !slot->valid)
        return false;
    view->data = slot->storage;
    view->size = slot->size;
    view->channel = slot->channel;
    view->type = slot->type;
    return true;
}

void afk_deferred_message_release(struct afk_deferred_message *slot)
{
    if (!slot)
        return;
    slot->valid = false;
    slot->size = 0;
}
