/* SPDX-License-Identifier: MIT */

#include "afk_raw_router.h"
#include "string.h"

void afk_raw_router_init(struct afk_raw_router *router)
{
    if (router)
        memset(router, 0, sizeof(*router));
}

bool afk_raw_router_register(struct afk_raw_router *router, afk_raw_u8 endpoint,
                             afk_raw_message_handler_t handler, void *opaque)
{
    struct afk_raw_route *free_route = NULL;

    if (!router || !handler)
        return false;

    for (size_t i = 0; i < AFK_RAW_ROUTER_MAX_HANDLERS; ++i) {
        struct afk_raw_route *route = &router->routes[i];

        if (route->handler && route->endpoint == endpoint)
            return false;
        if (!route->handler && !free_route)
            free_route = route;
    }

    if (!free_route)
        return false;

    free_route->endpoint = endpoint;
    free_route->handler = handler;
    free_route->opaque = opaque;
    return true;
}

bool afk_raw_router_unregister(struct afk_raw_router *router, afk_raw_u8 endpoint,
                               afk_raw_message_handler_t handler, void *opaque)
{
    if (!router || !handler)
        return false;

    for (size_t i = 0; i < AFK_RAW_ROUTER_MAX_HANDLERS; ++i) {
        struct afk_raw_route *route = &router->routes[i];

        if (route->handler != handler || route->endpoint != endpoint || route->opaque != opaque)
            continue;

        memset(route, 0, sizeof(*route));
        return true;
    }

    return false;
}

int afk_raw_router_dispatch(const struct afk_raw_router *router, afk_raw_u8 endpoint,
                            afk_raw_u64 message)
{
    if (!router)
        return AFK_RAW_NOT_HANDLED;

    for (size_t i = 0; i < AFK_RAW_ROUTER_MAX_HANDLERS; ++i) {
        const struct afk_raw_route *route = &router->routes[i];

        if (route->handler && route->endpoint == endpoint)
            return route->handler(route->opaque, endpoint, message);
    }

    return AFK_RAW_NOT_HANDLED;
}
