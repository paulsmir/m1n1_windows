/* SPDX-License-Identifier: MIT */

#ifndef DCP_AFK_DEFERRED_MESSAGE_H
#define DCP_AFK_DEFERRED_MESSAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct afk_deferred_message {
    uint8_t *storage;
    size_t capacity;
    size_t size;
    uint32_t channel;
    uint32_t type;
    bool valid;
};

struct afk_deferred_view {
    const uint8_t *data;
    size_t size;
    uint32_t channel;
    uint32_t type;
};

void afk_deferred_message_init(struct afk_deferred_message *slot, void *storage,
                               size_t capacity);
bool afk_deferred_message_store(struct afk_deferred_message *slot, uint32_t channel,
                                uint32_t type, const void *data, size_t size);
bool afk_deferred_message_take(const struct afk_deferred_message *slot,
                               struct afk_deferred_view *view);
void afk_deferred_message_release(struct afk_deferred_message *slot);

#endif
