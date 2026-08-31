/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdio.h>

#include "../src/afk_command_owner.h"

static void test_timeout_poison_blocks_reuse_until_exact_reply(void)
{
    struct afk_epic_command_owner owner;
    afk_epic_command_t first = {0};
    afk_epic_command_t second = {0};
    struct afk_epic_command_identity identity = {
        .channel = 3,
        .sub_type = 0xc0,
        .sequence = 17,
    };

    afk_epic_command_owner_init(&owner);
    assert(afk_epic_command_owner_claim(&owner, &first) == 0);
    assert(afk_epic_command_owner_poison(&owner, &first, &identity));
    assert(afk_epic_command_owner_claim(&owner, &second) == AFK_EPIC_COMMAND_BUSY);

    struct afk_epic_command_reply stale = {
        .channel = 3,
        .sub_type = 0xc0,
        .sequence = 18,
    };
    assert(!afk_epic_command_owner_retire_poison(&owner, &stale));
    assert(afk_epic_command_owner_claim(&owner, &second) == AFK_EPIC_COMMAND_BUSY);

    struct afk_epic_command_reply exact = {
        .channel = 3,
        .sub_type = 0xc0,
        .sequence = 17,
        .rxbuf_matches = true,
    };
    struct afk_epic_command_reply wrong_dma = exact;
    wrong_dma.rxbuf_matches = false;
    assert(!afk_epic_command_owner_retire_poison(&owner, &wrong_dma));
    assert(afk_epic_command_owner_claim(&owner, &second) == AFK_EPIC_COMMAND_BUSY);
    assert(afk_epic_command_owner_retire_poison(&owner, &exact));
    assert(afk_epic_command_owner_claim(&owner, &second) == 0);
    assert(afk_epic_command_owner_release(&owner, &second));
}

static void test_endpoint_reset_is_the_only_forced_poison_release(void)
{
    struct afk_epic_command_owner owner;
    afk_epic_command_t command = {0};
    struct afk_epic_command_identity identity = {1, 2, 3};

    afk_epic_command_owner_init(&owner);
    assert(afk_epic_command_owner_claim(&owner, &command) == 0);
    assert(afk_epic_command_owner_poison(&owner, &command, &identity));
    assert(afk_epic_command_owner_is_poisoned(&owner));
    afk_epic_command_owner_reset(&owner);
    assert(!afk_epic_command_owner_is_poisoned(&owner));
    assert(afk_epic_command_owner_claim(&owner, &command) == 0);
}

int main(void)
{
    test_timeout_poison_blocks_reuse_until_exact_reply();
    test_endpoint_reset_is_the_only_forced_poison_release();
    puts("afk_command_owner_test: ok");
    return 0;
}
