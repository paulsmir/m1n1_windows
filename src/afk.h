/* SPDX-License-Identifier: MIT */

#ifndef DCP_AFK_H
#define DCP_AFK_H

#include "afk_command.h"
#include "afk_raw_router.h"
#include "rtkit.h"
#include "types.h"

enum EPICMessage {
    SUBTYPE_ANNOUNCE = 0x30,
    SUBTYPE_STD_SERVICE = 0xc0,
};

typedef struct afk_epic afk_epic_t;

typedef struct afk_epic_service_ops afk_epic_service_ops_t;

typedef struct afk_epic_service {
    void *cookie;
    const afk_epic_service_ops_t *ops;
    afk_epic_ep_t *epic;
    void *intf;
    u32 channel;
    u16 seq;
    bool enabled;

} afk_epic_service_t;

typedef struct afk_epic_service_ops {
    const char name[32];

    void (*init)(afk_epic_service_t *service, const char *name, const char *eclass, s64 unit);
    int (*call)(afk_epic_service_t *service, u32 idx, const void *data, size_t data_size,
                void *reply, size_t reply_size);
} afk_epic_service_ops_t;

afk_epic_t *afk_epic_init(rtkit_dev_t *rtkit);
int afk_epic_shutdown(afk_epic_t *afk);

bool afk_epic_register_raw_handler(afk_epic_t *afk, u8 endpoint,
                                   afk_raw_message_handler_t handler, void *opaque);
bool afk_epic_unregister_raw_handler(afk_epic_t *afk, u8 endpoint,
                                     afk_raw_message_handler_t handler, void *opaque);

afk_epic_ep_t *afk_epic_start_ep(afk_epic_t *afk, int endpoint, const afk_epic_service_ops_t *ops,
                                 bool notify);
int afk_epic_shutdown_ep(afk_epic_ep_t *epic);

int afk_epic_work(afk_epic_t *afk, int endpoint);
/* Dispatch one preallocated notification deferred by the nonblocking command
 * poller.  This may invoke service callbacks and is normal-context only. */
int afk_epic_dispatch_deferred(afk_epic_ep_t *epic);
/* Drain at most one transport item while a timed-out command owns the shared
 * DMA buffers.  Ownership is released only by its exact late reply. */
int afk_epic_command_reap(afk_epic_ep_t *epic);
int afk_epic_start_interface(afk_epic_ep_t *epic, void *intf, int expected, size_t insize,
                             size_t outsize);
/* Finite compatibility wrapper for synchronous boot/bring-up callers only. */
int afk_epic_command(afk_epic_ep_t *epic, int channel, u16 sub_type, void *txbuf, size_t txsize,
                     void *rxbuf, size_t *rxsize);

#endif
