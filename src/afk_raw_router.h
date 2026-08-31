/* SPDX-License-Identifier: MIT */

#ifndef AFK_RAW_ROUTER_H
#define AFK_RAW_ROUTER_H

#include <limits.h>

#ifdef AFK_RAW_ROUTER_HOST_TEST
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t afk_raw_u8;
typedef uint64_t afk_raw_u64;
#else
#include "types.h"
typedef u8 afk_raw_u8;
typedef u64 afk_raw_u64;
#endif

#define AFK_RAW_ROUTER_MAX_HANDLERS 4
#define AFK_RAW_NOT_HANDLED         INT_MIN

typedef int (*afk_raw_message_handler_t)(void *opaque, afk_raw_u8 endpoint,
                                         afk_raw_u64 message);

struct afk_raw_route {
    afk_raw_u8 endpoint;
    afk_raw_message_handler_t handler;
    void *opaque;
};

struct afk_raw_router {
    struct afk_raw_route routes[AFK_RAW_ROUTER_MAX_HANDLERS];
};

void afk_raw_router_init(struct afk_raw_router *router);
bool afk_raw_router_register(struct afk_raw_router *router, afk_raw_u8 endpoint,
                             afk_raw_message_handler_t handler, void *opaque);
bool afk_raw_router_unregister(struct afk_raw_router *router, afk_raw_u8 endpoint,
                               afk_raw_message_handler_t handler, void *opaque);
int afk_raw_router_dispatch(const struct afk_raw_router *router,
                            afk_raw_u8 endpoint, afk_raw_u64 message);

#endif
