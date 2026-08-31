/* SPDX-License-Identifier: MIT */

#ifndef DCP_AFK_COMMAND_OWNER_H
#define DCP_AFK_COMMAND_OWNER_H

#include "afk_command_internal.h"
#include <stdint.h>

struct afk_epic_command_identity {
    uint32_t channel;
    uint16_t sub_type;
    uint16_t sequence;
};

struct afk_epic_command_owner {
    uintptr_t state;
    struct afk_epic_command_identity poisoned;
};

void afk_epic_command_owner_init(struct afk_epic_command_owner *owner);
int afk_epic_command_owner_claim(struct afk_epic_command_owner *owner,
                                 afk_epic_command_t *command);
bool afk_epic_command_owner_release(struct afk_epic_command_owner *owner,
                                    afk_epic_command_t *command);
bool afk_epic_command_owner_poison(struct afk_epic_command_owner *owner,
                                   afk_epic_command_t *command,
                                   const struct afk_epic_command_identity *identity);
bool afk_epic_command_owner_is_poisoned(const struct afk_epic_command_owner *owner);
bool afk_epic_command_owner_retire_poison(struct afk_epic_command_owner *owner,
                                          const struct afk_epic_command_reply *reply);
void afk_epic_command_owner_reset(struct afk_epic_command_owner *owner);

#endif
