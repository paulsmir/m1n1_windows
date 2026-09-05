/* SPDX-License-Identifier: MIT */

#include "adt.h"
#include "ans_queue.h"
#include "assert.h"
#include "malloc.h"
#include "nvme.h"
#include "pmgr.h"
#include "rtkit.h"
#include "sart.h"
#include "string.h"
#include "utils.h"

#define NVME_TIMEOUT          1000000
#define NVME_ENABLE_TIMEOUT   5000000
#define NVME_SHUTDOWN_TIMEOUT 5000000
#define NVME_QUEUE_SIZE       64
#define NVME_MAX_IO_BLOCKS    32

#define NVME_CC            0x14
#define NVME_CC_SHN        GENMASK(15, 14)
#define NVME_CC_SHN_NONE   0
#define NVME_CC_SHN_NORMAL 1
#define NVME_CC_SHN_ABRUPT 2
#define NVME_CC_EN         BIT(0)

#define NVME_CSTS             0x1c
#define NVME_CSTS_SHST        GENMASK(3, 2)
#define NVME_CSTS_SHST_NORMAL 0
#define NVME_CSTS_SHST_BUSY   1
#define NVME_CSTS_SHST_DONE   2
#define NVME_CSTS_RDY         BIT(0)

#define NVME_AQA 0x24
#define NVME_ASQ 0x28
#define NVME_ACQ 0x30

#define NVME_DB_ACQ  0x1004
#define NVME_DB_IOCQ 0x100c

#define NVME_BOOT_STATUS    0x1300
#define NVME_BOOT_STATUS_OK 0xde71ce55

#define NVME_LINEAR_SQ_CTRL    0x24908
#define NVME_LINEAR_SQ_CTRL_EN BIT(0)

#define NVME_UNKNOWN_CTRL                0x24008
#define NVME_UNKNOWN_CTRL_PRP_NULL_CHECK BIT(11)

#define NVME_MAX_PEND_CMDS_CTRL 0x1210
#define NVME_DB_LINEAR_ASQ      0x2490c
#define NVME_DB_LINEAR_IOSQ     0x24910

#define NVMMU_NUM       0x28100
#define NVMMU_ASQ_BASE  0x28108
#define NVMMU_IOSQ_BASE 0x28110
#define NVMMU_TCB_INVAL 0x28118
#define NVMMU_TCB_STAT  0x29120

#define NVME_ADMIN_CMD_DELETE_SQ 0x00
#define NVME_ADMIN_CMD_CREATE_SQ 0x01
#define NVME_ADMIN_CMD_DELETE_CQ 0x04
#define NVME_ADMIN_CMD_CREATE_CQ 0x05
#define NVME_ADMIN_CMD_IDENTIFY  0x06
#define NVME_QUEUE_CONTIGUOUS    BIT(0)

#define NVME_CMD_FLUSH 0x00
#define NVME_CMD_WRITE 0x01
#define NVME_CMD_READ  0x02

struct nvme_queue {
    struct apple_nvmmu_tcb *tcbs;
    struct nvme_command *cmds;
    struct nvme_completion *cqes;
    bool adminq;
};

static_assert(NVME_QUEUE_SIZE == ANS_HW_DEPTH, "ANS engine/backing depth mismatch");
static struct ans_queue_engine queue_engine;

static bool nvme_initialized = false;
static u8 nvme_die;

static asc_dev_t *nvme_asc = NULL;
static rtkit_dev_t *nvme_rtkit = NULL;
static sart_dev_t *nvme_sart = NULL;

static u64 nvme_base;

static struct nvme_queue adminq, ioq;
static u64 io_prp_list[SZ_4K / sizeof(u64)] ALIGNED(SZ_4K);

static bool alloc_queue(struct nvme_queue *q)
{
    memset(q, 0, sizeof(*q));

    q->tcbs = memalign(SZ_16K, NVME_QUEUE_SIZE * sizeof(*q->tcbs));
    if (!q->tcbs)
        return false;

    q->cmds = memalign(SZ_16K, NVME_QUEUE_SIZE * sizeof(*q->cmds));
    if (!q->cmds)
        goto free_tcbs;

    q->cqes = memalign(SZ_16K, NVME_QUEUE_SIZE * sizeof(*q->cqes));
    if (!q->cqes)
        goto free_cmds;

    memset(q->tcbs, 0, NVME_QUEUE_SIZE * sizeof(*q->tcbs));
    memset(q->cmds, 0, NVME_QUEUE_SIZE * sizeof(*q->cmds));
    memset(q->cqes, 0, NVME_QUEUE_SIZE * sizeof(*q->cqes));
    return true;

free_cmds:
    free(q->cmds);
free_tcbs:
    free(q->tcbs);
    return false;
}

static void free_queue(struct nvme_queue *q)
{
    free(q->cmds);
    free(q->tcbs);
    free(q->cqes);
}

static void nvme_poll_syslog(void)
{
    struct rtkit_message msg;
    rtkit_recv(nvme_rtkit, &msg);
}

static bool nvme_ctrl_disable(void)
{
    u64 timeout = timeout_calculate(NVME_TIMEOUT);

    clear32(nvme_base + NVME_CC, NVME_CC_EN);
    while (read32(nvme_base + NVME_CSTS) & NVME_CSTS_RDY && !timeout_expired(timeout))
        nvme_poll_syslog();

    return !(read32(nvme_base + NVME_CSTS) & NVME_CSTS_RDY);
}

static bool nvme_ctrl_enable(void)
{
    u64 timeout = timeout_calculate(NVME_ENABLE_TIMEOUT);

    mask32(nvme_base + NVME_CC, NVME_CC_SHN, NVME_CC_EN);
    while (!(read32(nvme_base + NVME_CSTS) & NVME_CSTS_RDY) && !timeout_expired(timeout))
        nvme_poll_syslog();

    return read32(nvme_base + NVME_CSTS) & NVME_CSTS_RDY;
}

static bool nvme_ctrl_shutdown(void)
{
    u64 timeout = timeout_calculate(NVME_SHUTDOWN_TIMEOUT);

    mask32(nvme_base + NVME_CC, NVME_CC_SHN, FIELD_PREP(NVME_CC_SHN, NVME_CC_SHN_NORMAL));
    while (FIELD_GET(NVME_CSTS_SHST, read32(nvme_base + NVME_CSTS)) != NVME_CSTS_SHST_DONE &&
           !timeout_expired(timeout))
        nvme_poll_syslog();

    return FIELD_GET(NVME_CSTS_SHST, read32(nvme_base + NVME_CSTS)) == NVME_CSTS_SHST_DONE;
}

static void queue_dma_wmb(void *ctx)
{
    UNUSED(ctx);
    dma_wmb();
}

static void queue_dma_rmb(void *ctx)
{
    UNUSED(ctx);
    dma_rmb();
}

static void queue_submit(void *ctx, unsigned queue, unsigned tag)
{
    UNUSED(ctx);
    nvme_poll_syslog();
    write32(nvme_base + (queue == ANS_HW_ADMIN ? NVME_DB_LINEAR_ASQ : NVME_DB_LINEAR_IOSQ),
            tag);
    nvme_poll_syslog();
}

static bool queue_invalidate(void *ctx, unsigned tag)
{
    UNUSED(ctx);
    write32(nvme_base + NVMMU_TCB_INVAL, tag);
    if (read32(nvme_base + NVMMU_TCB_STAT))
        printf("nvme: NVMMU invalidation for tag %d failed\n", tag);
    /* Recovery retains its historical advisory status read at 0x29120.
     * Native must supply its own proven invalidation receipt, not reuse this. */
    return true;
}

static void queue_ack(void *ctx, unsigned queue, unsigned head)
{
    UNUSED(ctx);
    write32(nvme_base + (queue == ANS_HW_ADMIN ? NVME_DB_ACQ : NVME_DB_IOCQ), head);
}

static bool nvme_exec_command(struct nvme_queue *q, struct nvme_command *cmd, u64 *result)
{
    unsigned queue = q->adminq ? ANS_HW_ADMIN : ANS_HW_IO;
    /* Preserve recovery's tag 0, command opcode and bidirectional TCB policy. */
    if (ans_queue_submit(&queue_engine, queue_engine.generation, queue, 0, cmd,
                         cmd->opcode, 3) != ANS_Q_OK) {
        printf("nvme: command queue unavailable\n");
        return false;
    }

    u64 timeout = timeout_calculate(NVME_TIMEOUT);
    struct nvme_completion cqe;
    while (!timeout_expired(timeout)) {
        nvme_poll_syslog();
        int ret = ans_queue_poll(&queue_engine, queue_engine.generation, queue, &cqe);
        if (ret == ANS_Q_EMPTY)
            continue;
        if (ret != ANS_Q_OK) {
            printf("nvme: invalid command completion\n");
            return false;
        }
        if (result)
            *result = cqe.result;
        if (cqe.status >> 1) {
            printf("nvme: command failed with status %d\n", cqe.status >> 1);
            return false;
        }
        return true;
    }

    /* A timeout does not return DMA ownership. Pin the tag until teardown;
     * publishing another tag 0 command could corrupt the outstanding request. */
    ans_queue_fault(&queue_engine);
    printf("nvme: could not find command completion in CQ\n");
    return false;
}

bool nvme_init(void)
{
    if (nvme_initialized) {
        printf("nvme: already initialized\n");
        return true;
    }

    int adt_path[8];
    int node = adt_path_offset_trace(adt, "/arm-io/ans", adt_path);
    if (node < 0) {
        printf("nvme: Error getting NVMe node /arm-io/ans\n");
        return NULL;
    }

    if (adt_get_reg(adt, adt_path, "reg", 3, &nvme_base, NULL) < 0) {
        printf("nvme: Error getting NVMe base address.\n");
        return NULL;
    }
    u32 cg;
    if (ADT_GETPROP(adt, node, "clock-gates", &cg) < 0) {
        printf("nvme: clock-gates not set\n");
        nvme_die = (nvme_base >> 37) & 3;
    } else {
        nvme_die = FIELD_GET(PMGR_DIE_ID, cg);
    }
    printf("nvme: ANS is on die %d\n", nvme_die);

    if (!alloc_queue(&adminq)) {
        printf("nvme: Error allocating admin queue\n");
        return NULL;
    }
    if (!alloc_queue(&ioq)) {
        printf("nvme: Error allocating admin queue\n");
        goto out_adminq;
    }

    ioq.adminq = false;
    adminq.adminq = true;

    const struct ans_hw_queue queues[2] = {
        {.cmds = adminq.cmds, .cqes = adminq.cqes, .tcbs = adminq.tcbs},
        {.cmds = ioq.cmds, .cqes = ioq.cqes, .tcbs = ioq.tcbs},
    };
    const struct ans_queue_ops ops = {
        .dma_wmb = queue_dma_wmb, .dma_rmb = queue_dma_rmb, .submit = queue_submit,
        .invalidate = queue_invalidate, .ack = queue_ack,
    };
    /* Fresh recovery allocation, before RTKit/controller boot. The existing
     * recovery teardown owns backing lifetime; this is not native stop proof. */
    if (ans_queue_init(&queue_engine, queues, &ops, NULL, 2, true) != ANS_Q_OK)
        goto out_ioq;

    nvme_asc = asc_init("/arm-io/ans");
    if (!nvme_asc)
        goto out_ioq;

    nvme_sart = sart_init("/arm-io/sart-ans");
    if (!nvme_sart)
        goto out_asc;

    nvme_rtkit = rtkit_init("nvme", nvme_asc, NULL, NULL, nvme_sart, false);
    if (!nvme_rtkit)
        goto out_sart;

    if (!rtkit_boot(nvme_rtkit))
        goto out_rtkit;

    if (poll32(nvme_base + NVME_BOOT_STATUS, 0xffffffff, NVME_BOOT_STATUS_OK, USEC_PER_SEC) < 0) {
        printf("nvme: ANS did not boot correctly.\n");
        goto out_shutdown;
    }

    /* setup controller and NVMMU for linear submission queue */
    set32(nvme_base + NVME_LINEAR_SQ_CTRL, NVME_LINEAR_SQ_CTRL_EN);
    clear32(nvme_base + NVME_UNKNOWN_CTRL, NVME_UNKNOWN_CTRL_PRP_NULL_CHECK);
    write32(nvme_base + NVME_MAX_PEND_CMDS_CTRL,
            ((NVME_QUEUE_SIZE - 1) << 16) | (NVME_QUEUE_SIZE - 1));
    write32(nvme_base + NVMMU_NUM, NVME_QUEUE_SIZE - 1);
    write64_lo_hi(nvme_base + NVMMU_ASQ_BASE, (u64)adminq.tcbs);
    write64_lo_hi(nvme_base + NVMMU_IOSQ_BASE, (u64)ioq.tcbs);

    /* setup admin queue */
    if (!nvme_ctrl_disable()) {
        printf("nvme: timeout while waiting for CSTS.RDY to clear\n");
        goto out_shutdown;
    }
    write64_lo_hi(nvme_base + NVME_ASQ, (u64)adminq.cmds);
    write64_lo_hi(nvme_base + NVME_ACQ, (u64)adminq.cqes);
    write32(nvme_base + NVME_AQA, ((NVME_QUEUE_SIZE - 1) << 16) | (NVME_QUEUE_SIZE - 1));
    if (!nvme_ctrl_enable()) {
        printf("nvme: timeout while waiting for CSTS.RDY to be set\n");
        goto out_disable_ctrl;
    }

    /* setup IO queue */
    struct nvme_command cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_CMD_CREATE_CQ;
    cmd.prp1 = (u64)ioq.cqes;
    cmd.cdw10 = 1; // cq id
    cmd.cdw10 |= (NVME_QUEUE_SIZE - 1) << 16;
    cmd.cdw11 = NVME_QUEUE_CONTIGUOUS;
    if (!nvme_exec_command(&adminq, &cmd, NULL)) {
        printf("nvme: create cq command failed\n");
        goto out_disable_ctrl;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_CMD_CREATE_SQ;
    cmd.prp1 = (u64)ioq.cmds;
    cmd.cdw10 = 1; // sq id
    cmd.cdw10 |= (NVME_QUEUE_SIZE - 1) << 16;
    cmd.cdw11 = NVME_QUEUE_CONTIGUOUS;
    cmd.cdw11 |= 1 << 16; // cq id for this sq
    if (!nvme_exec_command(&adminq, &cmd, NULL)) {
        printf("nvme: create sq command failed\n");
        goto out_delete_cq;
    }

    nvme_initialized = true;
    printf("nvme: initialized at 0x%lx\n", nvme_base);
    return true;

out_delete_cq:
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_CMD_DELETE_CQ;
    cmd.cdw10 = 1; // cq id
    if (!nvme_exec_command(&adminq, &cmd, NULL))
        printf("nvme: delete cq command failed\n");
out_disable_ctrl:
    nvme_ctrl_shutdown();
    nvme_ctrl_disable();
    nvme_poll_syslog();
out_shutdown:
    rtkit_sleep(nvme_rtkit);
    // Some machines call this ANS, some ANS2...
    pmgr_reset(nvme_die, "ANS");
    pmgr_reset(nvme_die, "ANS2");
out_rtkit:
    rtkit_free(nvme_rtkit);
out_sart:
    sart_free(nvme_sart);
out_asc:
    asc_free(nvme_asc);
out_ioq:
    free_queue(&ioq);
out_adminq:
    free_queue(&adminq);
    return false;
}

void nvme_ensure_shutdown(void)
{
    nvme_asc = asc_init("/arm-io/ans");
    if (!nvme_asc)
        return;

    if (!asc_cpu_running(nvme_asc)) {
        printf("nvme: ANS not running\n");
        asc_free(nvme_asc);
        nvme_asc = NULL;
        return;
    }

    printf("nvme: Found ANS left powered, doing a proper shutdown\n");

    nvme_sart = sart_init("/arm-io/sart-ans");
    if (!nvme_sart)
        goto fail;

    nvme_rtkit = rtkit_init("nvme", nvme_asc, NULL, NULL, nvme_sart, false);
    if (!nvme_rtkit)
        goto fail;

    if (!rtkit_boot(nvme_rtkit))
        goto fail;

    rtkit_sleep(nvme_rtkit);

fail:
    if (nvme_rtkit) {
        rtkit_free(nvme_rtkit);
        nvme_rtkit = NULL;
    }
    if (nvme_sart) {
        sart_free(nvme_sart);
        nvme_sart = NULL;
    }
    asc_free(nvme_asc);
    nvme_asc = NULL;

    // Some machines call this ANS, some ANS2...
    pmgr_reset(nvme_die, "ANS");
    pmgr_reset(nvme_die, "ANS2");
}

void nvme_shutdown(void)
{
    if (!nvme_initialized) {
        // nvme_ensure_shutdown();
        return;
    }

    struct nvme_command cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_CMD_DELETE_SQ;
    cmd.cdw10 = 1; // sq id
    if (!nvme_exec_command(&adminq, &cmd, NULL))
        printf("nvme: delete sq command failed\n");

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_CMD_DELETE_CQ;
    cmd.cdw10 = 1; // cq id
    if (!nvme_exec_command(&adminq, &cmd, NULL))
        printf("nvme: delete cq command failed\n");

    if (!nvme_ctrl_shutdown())
        printf("nvme: timeout while waiting for controller shutdown\n");
    if (!nvme_ctrl_disable())
        printf("nvme: timeout while waiting for CSTS.RDY to clear\n");

    rtkit_sleep(nvme_rtkit);
    // Some machines call this ANS, some ANS2...
    pmgr_reset(nvme_die, "ANS");
    pmgr_reset(nvme_die, "ANS2");
    rtkit_free(nvme_rtkit);
    sart_free(nvme_sart);
    asc_free(nvme_asc);
    free_queue(&ioq);
    free_queue(&adminq);
    nvme_initialized = false;

    printf("nvme: shutdown done\n");
}

bool nvme_flush(u32 nsid)
{
    struct nvme_command cmd;

    if (!nvme_initialized)
        return false;

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_CMD_FLUSH;
    cmd.nsid = nsid;

    return nvme_exec_command(&ioq, &cmd, NULL);
}

static bool nvme_rw_blocks(u32 nsid, u64 lba, u32 blocks, void *buffer, bool write)
{
    struct nvme_command cmd;
    u64 buffer_addr = (u64)buffer;

    if (!nvme_initialized || !blocks || blocks > NVME_MAX_IO_BLOCKS)
        return false;

    /* ANS uses standard 4 KiB NVMe PRPs even though the CPU page size is 16 KiB. */
    if (buffer_addr & (SZ_4K - 1))
        return false;

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = write ? NVME_CMD_WRITE : NVME_CMD_READ;
    cmd.nsid = nsid;
    cmd.prp1 = (u64)buffer_addr;
    if (blocks == 2) {
        cmd.prp2 = buffer_addr + SZ_4K;
    } else if (blocks > 2) {
        for (u32 i = 1; i < blocks; i++)
            io_prp_list[i - 1] = buffer_addr + (u64)i * SZ_4K;
        dma_wmb();
        cmd.prp2 = (u64)io_prp_list;
    }
    cmd.cdw10 = lba;
    cmd.cdw11 = lba >> 32;
    cmd.cdw12 = blocks - 1; // NLB is zero-based

    return nvme_exec_command(&ioq, &cmd, NULL);
}

bool nvme_read_blocks(u32 nsid, u64 lba, u32 blocks, void *buffer)
{
    return nvme_rw_blocks(nsid, lba, blocks, buffer, false);
}

bool nvme_write_blocks(u32 nsid, u64 lba, u32 blocks, const void *buffer)
{
    return nvme_rw_blocks(nsid, lba, blocks, (void *)buffer, true);
}

bool nvme_read(u32 nsid, u64 lba, void *buffer)
{
    return nvme_read_blocks(nsid, lba, 1, buffer);
}

bool nvme_write(u32 nsid, u64 lba, const void *buffer)
{
    return nvme_write_blocks(nsid, lba, 1, buffer);
}

bool nvme_get_namespace_info(u32 nsid, u64 *blocks, u32 *lba_size)
{
    if (!nvme_initialized || !blocks || !lba_size)
        return false;

    u8 *identify = memalign(SZ_4K, SZ_4K);
    if (!identify)
        return false;
    memset(identify, 0, SZ_4K);

    struct nvme_command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_CMD_IDENTIFY;
    cmd.nsid = nsid;
    cmd.prp1 = (u64)identify;
    cmd.cdw10 = 0; // CNS 0: Identify Namespace

    bool ok = nvme_exec_command(&adminq, &cmd, NULL);
    if (ok) {
        memcpy(blocks, identify, sizeof(*blocks));
        u8 flbas = identify[26] & 0xf;
        u8 lbads = identify[128 + flbas * 4 + 2];
        if (!*blocks || lbads >= 32)
            ok = false;
        else
            *lba_size = 1u << lbads;
    }

    free(identify);
    return ok;
}
