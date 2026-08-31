/* SPDX-License-Identifier: MIT */

#include "afk_command.h"
#include "afk_command_internal.h"
#include "string.h"

static void afk_epic_command_finish(afk_epic_command_t *command, enum afk_epic_command_state state,
                                    int result)
{
    command->state = state;
    command->result = result;
    afk_epic_command_backend_release(command->epic, command);
}

static void afk_epic_command_timeout(afk_epic_command_t *command)
{
    command->state = AFK_EPIC_COMMAND_STATE_TIMED_OUT;
    command->result = AFK_EPIC_COMMAND_TIMEOUT;
    afk_epic_command_backend_poison(command->epic, command);
}

void afk_epic_command_init(afk_epic_command_t *command)
{
    if (command)
        memset(command, 0, sizeof(*command));
}

int afk_epic_command_submit(afk_epic_command_t *command, afk_epic_ep_t *epic, uint32_t channel,
                            uint16_t sub_type, const void *txbuf, size_t txsize, void *rxbuf,
                            size_t rxsize, uint64_t deadline)
{
    int ret;

    if (!command || !epic || (txsize && !txbuf) || (rxsize && !rxbuf))
        return AFK_EPIC_COMMAND_INVALID;
    if (command->state != AFK_EPIC_COMMAND_STATE_IDLE)
        return AFK_EPIC_COMMAND_INVALID;

    ret = afk_epic_command_backend_claim(epic, command);
    if (ret)
        return ret;

    command->epic = epic;
    command->channel = channel;
    command->sub_type = sub_type;
    command->deadline = deadline;
    command->rxbuf = rxbuf;
    command->rx_capacity = rxsize;
    command->rx_size = 0;
    command->result = 0;

    ret = afk_epic_command_backend_submit(epic, channel, sub_type, txbuf, txsize, rxsize,
                                          &command->sequence);
    if (ret) {
        afk_epic_command_finish(command, AFK_EPIC_COMMAND_STATE_FAILED, ret);
        return ret;
    }

    command->state = AFK_EPIC_COMMAND_STATE_PENDING;
    return 0;
}

static bool afk_epic_command_reply_matches(const afk_epic_command_t *command,
                                           const struct afk_epic_command_reply *reply)
{
    return reply->channel == command->channel && reply->type == AFK_EPIC_COMMAND_TYPE_REPLY &&
           reply->category == AFK_EPIC_COMMAND_CATEGORY_REPLY &&
           reply->sub_type == command->sub_type && reply->sequence == command->sequence &&
           reply->rxbuf_matches;
}

enum afk_epic_command_poll_result afk_epic_command_poll(afk_epic_command_t *command, uint64_t now)
{
    struct afk_epic_command_reply reply;
    int ret;

    if (!command)
        return AFK_EPIC_COMMAND_FAILED;

    switch (command->state) {
        case AFK_EPIC_COMMAND_STATE_COMPLETE:
            return AFK_EPIC_COMMAND_COMPLETE;
        case AFK_EPIC_COMMAND_STATE_FAILED:
            return AFK_EPIC_COMMAND_FAILED;
        case AFK_EPIC_COMMAND_STATE_TIMED_OUT:
            return AFK_EPIC_COMMAND_TIMED_OUT;
        case AFK_EPIC_COMMAND_STATE_IDLE:
            command->result = AFK_EPIC_COMMAND_INVALID;
            command->state = AFK_EPIC_COMMAND_STATE_FAILED;
            return AFK_EPIC_COMMAND_FAILED;
        case AFK_EPIC_COMMAND_STATE_PENDING:
            break;
    }

    if ((int64_t)(now - command->deadline) >= 0) {
        afk_epic_command_timeout(command);
        return AFK_EPIC_COMMAND_TIMED_OUT;
    }

    ret = afk_epic_command_backend_poll_one(command->epic, &reply);
    if (ret < 0) {
        afk_epic_command_finish(command, AFK_EPIC_COMMAND_STATE_FAILED, ret);
        return AFK_EPIC_COMMAND_FAILED;
    }
    if (!ret)
        return AFK_EPIC_COMMAND_PENDING;

    if (!afk_epic_command_reply_matches(command, &reply)) {
        afk_epic_command_backend_consume(command->epic);
        return AFK_EPIC_COMMAND_PENDING;
    }

    if (reply.retcode) {
        afk_epic_command_backend_consume(command->epic);
        afk_epic_command_finish(command, AFK_EPIC_COMMAND_STATE_COMPLETE, (int)reply.retcode);
        return AFK_EPIC_COMMAND_COMPLETE;
    }

    if (reply.rxlen > command->rx_capacity) {
        afk_epic_command_backend_consume(command->epic);
        afk_epic_command_finish(command, AFK_EPIC_COMMAND_STATE_FAILED, AFK_EPIC_COMMAND_OVERFLOW);
        return AFK_EPIC_COMMAND_FAILED;
    }

    if (reply.rxlen)
        afk_epic_command_backend_copy_reply(command->epic, command->rxbuf, reply.rxlen);
    command->rx_size = reply.rxlen;
    afk_epic_command_backend_consume(command->epic);
    afk_epic_command_finish(command, AFK_EPIC_COMMAND_STATE_COMPLETE, 0);
    return AFK_EPIC_COMMAND_COMPLETE;
}

int afk_epic_command_result(const afk_epic_command_t *command)
{
    if (!command || command->state == AFK_EPIC_COMMAND_STATE_IDLE ||
        command->state == AFK_EPIC_COMMAND_STATE_PENDING)
        return AFK_EPIC_COMMAND_INVALID;
    return command->result;
}

size_t afk_epic_command_reply_size(const afk_epic_command_t *command)
{
    if (!command || command->state != AFK_EPIC_COMMAND_STATE_COMPLETE)
        return 0;
    return command->rx_size;
}
