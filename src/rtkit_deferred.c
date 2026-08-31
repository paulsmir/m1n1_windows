/* SPDX-License-Identifier: MIT */

#include "rtkit_deferred.h"
#include "string.h"

void rtkit_deferred_init(struct rtkit_deferred_queue *queue)
{
    if (queue)
        memset(queue, 0, sizeof(*queue));
}

bool rtkit_deferred_push(struct rtkit_deferred_queue *queue,
                         const struct rtkit_deferred_message *message)
{
    unsigned int write;

    if (!queue || !message || queue->count == RTKIT_DEFERRED_CAPACITY)
        return false;
    write = (queue->read + queue->count) % RTKIT_DEFERRED_CAPACITY;
    queue->entries[write] = *message;
    queue->count++;
    return true;
}

bool rtkit_deferred_pop(struct rtkit_deferred_queue *queue,
                        struct rtkit_deferred_message *message)
{
    if (!queue || !message || !queue->count)
        return false;
    *message = queue->entries[queue->read];
    queue->read = (queue->read + 1) % RTKIT_DEFERRED_CAPACITY;
    queue->count--;
    return true;
}

bool rtkit_deferred_full(const struct rtkit_deferred_queue *queue)
{
    return queue && queue->count == RTKIT_DEFERRED_CAPACITY;
}
