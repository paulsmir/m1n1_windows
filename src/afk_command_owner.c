/* SPDX-License-Identifier: MIT */

#include "afk_command_owner.h"
#include "string.h"

#define AFK_EPIC_COMMAND_OWNER_FREE     ((uintptr_t)0)
#define AFK_EPIC_COMMAND_OWNER_POISONED ((uintptr_t)1)

void afk_epic_command_owner_init(struct afk_epic_command_owner *owner)
{
    if (owner)
        memset(owner, 0, sizeof(*owner));
}

int afk_epic_command_owner_claim(struct afk_epic_command_owner *owner,
                                 afk_epic_command_t *command)
{
    uintptr_t expected = AFK_EPIC_COMMAND_OWNER_FREE;
    uintptr_t desired = (uintptr_t)command;

    if (!owner || !command || desired <= AFK_EPIC_COMMAND_OWNER_POISONED)
        return AFK_EPIC_COMMAND_INVALID;
    if (!__atomic_compare_exchange_n(&owner->state, &expected, desired, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
        return AFK_EPIC_COMMAND_BUSY;
    return 0;
}

bool afk_epic_command_owner_release(struct afk_epic_command_owner *owner,
                                    afk_epic_command_t *command)
{
    uintptr_t expected = (uintptr_t)command;

    return owner && command &&
           __atomic_compare_exchange_n(&owner->state, &expected,
                                       AFK_EPIC_COMMAND_OWNER_FREE, false,
                                       __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

bool afk_epic_command_owner_poison(struct afk_epic_command_owner *owner,
                                   afk_epic_command_t *command,
                                   const struct afk_epic_command_identity *identity)
{
    uintptr_t expected = (uintptr_t)command;

    if (!owner || !command || !identity)
        return false;

    owner->poisoned = *identity;
    return __atomic_compare_exchange_n(&owner->state, &expected,
                                       AFK_EPIC_COMMAND_OWNER_POISONED, false,
                                       __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

bool afk_epic_command_owner_is_poisoned(const struct afk_epic_command_owner *owner)
{
    return owner && __atomic_load_n(&owner->state, __ATOMIC_ACQUIRE) ==
                        AFK_EPIC_COMMAND_OWNER_POISONED;
}

bool afk_epic_command_owner_retire_poison(struct afk_epic_command_owner *owner,
                                          const struct afk_epic_command_reply *reply)
{
    uintptr_t expected = AFK_EPIC_COMMAND_OWNER_POISONED;

    if (!owner || !reply || !afk_epic_command_owner_is_poisoned(owner))
        return false;
    if (!reply->rxbuf_matches || reply->channel != owner->poisoned.channel ||
        reply->sub_type != owner->poisoned.sub_type ||
        reply->sequence != owner->poisoned.sequence)
        return false;

    if (!__atomic_compare_exchange_n(&owner->state, &expected,
                                     AFK_EPIC_COMMAND_OWNER_FREE, false,
                                     __ATOMIC_RELEASE, __ATOMIC_RELAXED))
        return false;
    memset(&owner->poisoned, 0, sizeof(owner->poisoned));
    return true;
}

void afk_epic_command_owner_reset(struct afk_epic_command_owner *owner)
{
    if (!owner)
        return;
    memset(&owner->poisoned, 0, sizeof(owner->poisoned));
    __atomic_store_n(&owner->state, AFK_EPIC_COMMAND_OWNER_FREE, __ATOMIC_RELEASE);
}
