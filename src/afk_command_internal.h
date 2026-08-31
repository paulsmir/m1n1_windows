/* SPDX-License-Identifier: MIT */

#ifndef DCP_AFK_COMMAND_INTERNAL_H
#define DCP_AFK_COMMAND_INTERNAL_H

#include "afk_command.h"

enum afk_epic_command_message_type {
    AFK_EPIC_COMMAND_TYPE_NOTIFY = 0,
    AFK_EPIC_COMMAND_TYPE_REPLY = 4,
};

enum afk_epic_command_message_category {
    AFK_EPIC_COMMAND_CATEGORY_REPLY = 0x20,
};

struct afk_epic_command_reply {
    uint32_t channel;
    uint32_t type;
    uint8_t category;
    uint16_t sub_type;
    uint16_t sequence;
    uint32_t retcode;
    size_t rxlen;
    bool rxbuf_matches;
};

int afk_epic_command_backend_claim(afk_epic_ep_t *epic, afk_epic_command_t *command);
void afk_epic_command_backend_release(afk_epic_ep_t *epic, afk_epic_command_t *command);
/*
 * Preserve endpoint/DMA ownership after a submitted command times out.  The
 * backend must retain the command identity independently of caller storage
 * until the matching late reply is drained or the endpoint is reset.
 */
void afk_epic_command_backend_poison(afk_epic_ep_t *epic, afk_epic_command_t *command);
int afk_epic_command_backend_submit(afk_epic_ep_t *epic, uint32_t channel, uint16_t sub_type,
                                    const void *txbuf, size_t txsize, size_t rxsize,
                                    uint16_t *sequence);
int afk_epic_command_backend_poll_one(afk_epic_ep_t *epic, struct afk_epic_command_reply *reply);
void afk_epic_command_backend_consume(afk_epic_ep_t *epic);
void afk_epic_command_backend_copy_reply(afk_epic_ep_t *epic, void *rxbuf, size_t rxsize);

#endif
