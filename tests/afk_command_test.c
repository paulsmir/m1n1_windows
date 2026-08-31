/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/afk_command.h"

#include "../src/afk_command_internal.h"

struct afk_epic_ep {
    int unused;
};

static struct afk_epic_ep endpoint;
static afk_epic_command_t *claimed;
static int claim_count;
static int release_count;
static int poison_count;
static int submit_count;
static int poll_count;
static int consume_count;
static int copy_count;
static int submit_result;
static int poll_result;
static uint16_t submitted_sequence;
static struct afk_epic_command_reply next_reply;
static const unsigned char reply_bytes[] = {0x12, 0x34, 0x56, 0x78};

static void reset_backend(void)
{
    claimed = NULL;
    claim_count = 0;
    release_count = 0;
    poison_count = 0;
    submit_count = 0;
    poll_count = 0;
    consume_count = 0;
    copy_count = 0;
    submit_result = 0;
    poll_result = 0;
    submitted_sequence = 7;
    memset(&next_reply, 0, sizeof(next_reply));
}

int afk_epic_command_backend_claim(afk_epic_ep_t *epic, afk_epic_command_t *command)
{
    assert(epic == &endpoint);
    claim_count++;
    if (claimed)
        return AFK_EPIC_COMMAND_BUSY;
    claimed = command;
    return 0;
}

void afk_epic_command_backend_release(afk_epic_ep_t *epic, afk_epic_command_t *command)
{
    assert(epic == &endpoint);
    assert(claimed == command);
    release_count++;
    claimed = NULL;
}

void afk_epic_command_backend_poison(afk_epic_ep_t *epic, afk_epic_command_t *command)
{
    assert(epic == &endpoint);
    assert(claimed == command);
    poison_count++;
    claimed = NULL;
}

int afk_epic_command_backend_submit(afk_epic_ep_t *epic, uint32_t channel, uint16_t sub_type,
                                    const void *txbuf, size_t txsize, size_t rxsize,
                                    uint16_t *sequence)
{
    static const unsigned char expected_tx[] = {0xaa, 0xbb};

    assert(epic == &endpoint);
    assert(channel == 3);
    assert(sub_type == 0xc0);
    assert(txsize == sizeof(expected_tx));
    assert(memcmp(txbuf, expected_tx, sizeof(expected_tx)) == 0);
    assert(rxsize == sizeof(reply_bytes));
    submit_count++;
    *sequence = submitted_sequence;
    return submit_result;
}

int afk_epic_command_backend_poll_one(afk_epic_ep_t *epic, struct afk_epic_command_reply *reply)
{
    assert(epic == &endpoint);
    poll_count++;
    if (poll_result > 0)
        *reply = next_reply;
    return poll_result;
}

void afk_epic_command_backend_consume(afk_epic_ep_t *epic)
{
    assert(epic == &endpoint);
    consume_count++;
}

void afk_epic_command_backend_copy_reply(afk_epic_ep_t *epic, void *rxbuf, size_t rxsize)
{
    assert(epic == &endpoint);
    assert(rxsize <= sizeof(reply_bytes));
    copy_count++;
    memcpy(rxbuf, reply_bytes, rxsize);
}

static void init_command(afk_epic_command_t *command, unsigned char *rxbuf)
{
    static const unsigned char txbuf[] = {0xaa, 0xbb};

    afk_epic_command_init(command);
    assert(afk_epic_command_submit(command, &endpoint, 3, 0xc0, txbuf, sizeof(txbuf), rxbuf,
                                   sizeof(reply_bytes), 100) == 0);
}

static struct afk_epic_command_reply matching_reply(void)
{
    return (struct afk_epic_command_reply){
        .channel = 3,
        .type = AFK_EPIC_COMMAND_TYPE_REPLY,
        .category = AFK_EPIC_COMMAND_CATEGORY_REPLY,
        .sub_type = 0xc0,
        .sequence = submitted_sequence,
        .retcode = 0,
        .rxlen = sizeof(reply_bytes),
        .rxbuf_matches = true,
    };
}

static void test_submit_is_exactly_once(void)
{
    afk_epic_command_t command;
    unsigned char rxbuf[sizeof(reply_bytes)];
    static const unsigned char txbuf[] = {0xaa, 0xbb};

    reset_backend();
    init_command(&command, rxbuf);
    assert(submit_count == 1);
    assert(claim_count == 1);
    assert(afk_epic_command_submit(&command, &endpoint, 3, 0xc0, txbuf, sizeof(txbuf), rxbuf,
                                   sizeof(rxbuf), 200) == AFK_EPIC_COMMAND_INVALID);
    assert(submit_count == 1);
    assert(claim_count == 1);
}

static void test_second_command_is_busy_without_transmit(void)
{
    afk_epic_command_t first;
    afk_epic_command_t second;
    unsigned char rxbuf[sizeof(reply_bytes)];
    static const unsigned char txbuf[] = {0xaa, 0xbb};

    reset_backend();
    init_command(&first, rxbuf);
    afk_epic_command_init(&second);
    assert(afk_epic_command_submit(&second, &endpoint, 3, 0xc0, txbuf, sizeof(txbuf), rxbuf,
                                   sizeof(rxbuf), 100) == AFK_EPIC_COMMAND_BUSY);
    assert(submit_count == 1);
}

static void test_poll_ignores_nonmatching_reply_then_completes(void)
{
    afk_epic_command_t command;
    unsigned char rxbuf[sizeof(reply_bytes)] = {0};

    reset_backend();
    init_command(&command, rxbuf);
    poll_result = 1;
    next_reply = matching_reply();
    next_reply.sequence++;
    assert(afk_epic_command_poll(&command, 10) == AFK_EPIC_COMMAND_PENDING);
    assert(poll_count == 1);
    assert(consume_count == 1);
    assert(copy_count == 0);
    assert(release_count == 0);

    next_reply = matching_reply();
    assert(afk_epic_command_poll(&command, 11) == AFK_EPIC_COMMAND_COMPLETE);
    assert(poll_count == 2);
    assert(consume_count == 2);
    assert(copy_count == 1);
    assert(release_count == 1);
    assert(memcmp(rxbuf, reply_bytes, sizeof(rxbuf)) == 0);
    assert(afk_epic_command_result(&command) == 0);
    assert(afk_epic_command_reply_size(&command) == sizeof(reply_bytes));
}

static void test_every_reply_identity_field_must_match(void)
{
    afk_epic_command_t command;
    unsigned char rxbuf[sizeof(reply_bytes)];

    reset_backend();
    init_command(&command, rxbuf);
    poll_result = 1;

    next_reply = matching_reply();
    next_reply.channel++;
    assert(afk_epic_command_poll(&command, 1) == AFK_EPIC_COMMAND_PENDING);
    next_reply = matching_reply();
    next_reply.type = AFK_EPIC_COMMAND_TYPE_NOTIFY;
    assert(afk_epic_command_poll(&command, 2) == AFK_EPIC_COMMAND_PENDING);
    next_reply = matching_reply();
    next_reply.category--;
    assert(afk_epic_command_poll(&command, 3) == AFK_EPIC_COMMAND_PENDING);
    next_reply = matching_reply();
    next_reply.sub_type++;
    assert(afk_epic_command_poll(&command, 4) == AFK_EPIC_COMMAND_PENDING);
    next_reply = matching_reply();
    next_reply.sequence++;
    assert(afk_epic_command_poll(&command, 5) == AFK_EPIC_COMMAND_PENDING);
    next_reply = matching_reply();
    next_reply.rxbuf_matches = false;
    assert(afk_epic_command_poll(&command, 6) == AFK_EPIC_COMMAND_PENDING);

    assert(poll_count == 6);
    assert(consume_count == 6);
    assert(copy_count == 0);
    assert(release_count == 0);
}

static void test_poll_is_one_step_and_terminal_is_idempotent(void)
{
    afk_epic_command_t command;
    unsigned char rxbuf[sizeof(reply_bytes)];

    reset_backend();
    init_command(&command, rxbuf);
    poll_result = 0;
    assert(afk_epic_command_poll(&command, 1) == AFK_EPIC_COMMAND_PENDING);
    assert(poll_count == 1);

    poll_result = 1;
    next_reply = matching_reply();
    assert(afk_epic_command_poll(&command, 2) == AFK_EPIC_COMMAND_COMPLETE);
    assert(afk_epic_command_poll(&command, 3) == AFK_EPIC_COMMAND_COMPLETE);
    assert(poll_count == 2);
    assert(consume_count == 1);
    assert(release_count == 1);
}

static void test_deadline_is_bounded_and_inclusive(void)
{
    afk_epic_command_t command;
    unsigned char rxbuf[sizeof(reply_bytes)];

    reset_backend();
    init_command(&command, rxbuf);
    assert(afk_epic_command_poll(&command, 99) == AFK_EPIC_COMMAND_PENDING);
    assert(afk_epic_command_poll(&command, 100) == AFK_EPIC_COMMAND_TIMED_OUT);
    assert(afk_epic_command_poll(&command, 101) == AFK_EPIC_COMMAND_TIMED_OUT);
    assert(poll_count == 1);
    assert(release_count == 0);
    assert(poison_count == 1);
    assert(afk_epic_command_result(&command) == AFK_EPIC_COMMAND_TIMEOUT);
}

static void test_iop_error_completes_without_copy(void)
{
    afk_epic_command_t command;
    unsigned char rxbuf[sizeof(reply_bytes)];

    reset_backend();
    init_command(&command, rxbuf);
    poll_result = 1;
    next_reply = matching_reply();
    next_reply.retcode = 0xfffffff5;
    assert(afk_epic_command_poll(&command, 1) == AFK_EPIC_COMMAND_COMPLETE);
    assert(afk_epic_command_result(&command) == (int)0xfffffff5);
    assert(copy_count == 0);
    assert(consume_count == 1);
    assert(release_count == 1);
}

static void test_oversized_reply_fails_closed(void)
{
    afk_epic_command_t command;
    unsigned char rxbuf[sizeof(reply_bytes)];

    reset_backend();
    init_command(&command, rxbuf);
    poll_result = 1;
    next_reply = matching_reply();
    next_reply.rxlen = sizeof(reply_bytes) + 1;
    assert(afk_epic_command_poll(&command, 1) == AFK_EPIC_COMMAND_FAILED);
    assert(afk_epic_command_result(&command) == AFK_EPIC_COMMAND_OVERFLOW);
    assert(copy_count == 0);
    assert(consume_count == 1);
    assert(release_count == 1);
}

static void test_transport_error_is_terminal(void)
{
    afk_epic_command_t command;
    unsigned char rxbuf[sizeof(reply_bytes)];

    reset_backend();
    init_command(&command, rxbuf);
    poll_result = -77;
    assert(afk_epic_command_poll(&command, 1) == AFK_EPIC_COMMAND_FAILED);
    assert(afk_epic_command_result(&command) == -77);
    assert(release_count == 1);
}

int main(void)
{
    test_submit_is_exactly_once();
    test_second_command_is_busy_without_transmit();
    test_poll_ignores_nonmatching_reply_then_completes();
    test_every_reply_identity_field_must_match();
    test_poll_is_one_step_and_terminal_is_idempotent();
    test_deadline_is_bounded_and_inclusive();
    test_iop_error_completes_without_copy();
    test_oversized_reply_fails_closed();
    test_transport_error_is_terminal();
    puts("afk_command_test: ok");
    return 0;
}
