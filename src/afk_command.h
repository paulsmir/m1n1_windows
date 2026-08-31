/* SPDX-License-Identifier: MIT */

#ifndef DCP_AFK_COMMAND_H
#define DCP_AFK_COMMAND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct afk_epic_ep afk_epic_ep_t;
typedef struct afk_epic_command afk_epic_command_t;

enum afk_epic_command_poll_result {
    AFK_EPIC_COMMAND_TIMED_OUT = -2,
    AFK_EPIC_COMMAND_FAILED = -1,
    AFK_EPIC_COMMAND_PENDING = 0,
    AFK_EPIC_COMMAND_COMPLETE = 1,
};

enum afk_epic_command_error {
    AFK_EPIC_COMMAND_BUSY = -16,
    AFK_EPIC_COMMAND_INVALID = -22,
    AFK_EPIC_COMMAND_OVERFLOW = -75,
    AFK_EPIC_COMMAND_TIMEOUT = -110,
};

enum afk_epic_command_state {
    AFK_EPIC_COMMAND_STATE_IDLE,
    AFK_EPIC_COMMAND_STATE_PENDING,
    AFK_EPIC_COMMAND_STATE_COMPLETE,
    AFK_EPIC_COMMAND_STATE_FAILED,
    AFK_EPIC_COMMAND_STATE_TIMED_OUT,
};

struct afk_epic_command {
    afk_epic_ep_t *epic;
    void *rxbuf;
    size_t rx_capacity;
    size_t rx_size;
    uint64_t deadline;
    int result;
    uint32_t channel;
    uint16_t sub_type;
    uint16_t sequence;
    enum afk_epic_command_state state;
};

/*
 * A command is caller-owned and one-shot: initialize it, submit it once, and
 * keep both the command and rxbuf alive until poll returns a terminal status.
 * submit copies txbuf before returning. deadline and now use the same wrapping
 * 64-bit tick domain; now == deadline is timed out. The endpoint must already
 * be fully started with its command DMA buffers allocated.
 *
 * Only one command may own an endpoint because its DMA buffers are shared.
 * Exactly one execution context must poll that endpoint, and afk_epic_work()
 * must not consume the same endpoint concurrently. One poll performs at most
 * one transport step and consumes at most one queue entry; unmatched entries
 * are acknowledged and dropped. Poll never waits, logs, or allocates.
 *
 * Terminal polls are idempotent. Call init again only after consuming result
 * and reply_size if the same command storage is to be reused.
 */
void afk_epic_command_init(afk_epic_command_t *command);
int afk_epic_command_submit(afk_epic_command_t *command, afk_epic_ep_t *epic, uint32_t channel,
                            uint16_t sub_type, const void *txbuf, size_t txsize, void *rxbuf,
                            size_t rxsize, uint64_t deadline);
enum afk_epic_command_poll_result afk_epic_command_poll(afk_epic_command_t *command, uint64_t now);
int afk_epic_command_result(const afk_epic_command_t *command);
size_t afk_epic_command_reply_size(const afk_epic_command_t *command);

#endif
