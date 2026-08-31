/* SPDX-License-Identifier: MIT */

#ifndef RTKIT_DEFERRED_H
#define RTKIT_DEFERRED_H

#include <stdbool.h>
#include <stdint.h>

#define RTKIT_DEFERRED_CAPACITY 8

struct rtkit_deferred_message {
    uint8_t ep;
    uint64_t msg;
};

struct rtkit_deferred_queue {
    struct rtkit_deferred_message entries[RTKIT_DEFERRED_CAPACITY];
    unsigned int read;
    unsigned int count;
};

void rtkit_deferred_init(struct rtkit_deferred_queue *queue);
bool rtkit_deferred_push(struct rtkit_deferred_queue *queue,
                         const struct rtkit_deferred_message *message);
bool rtkit_deferred_pop(struct rtkit_deferred_queue *queue,
                        struct rtkit_deferred_message *message);
bool rtkit_deferred_full(const struct rtkit_deferred_queue *queue);

#endif
