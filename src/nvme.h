/* SPDX-License-Identifier: MIT */

#ifndef NVME_H
#define NVME_H

#include "types.h"
#include "ans_queue.h"

bool nvme_init(void);
void nvme_shutdown(void);
/* Trusted controller-lock observation, not a guest-supplied drain receipt. */
bool nvme_recovery_idle(void);
/* Controller lock required. Reservation persists until the next m1n1 world.
 * Public recovery access is closed immediately, before stop/native init. */
bool nvme_begin_native_handoff(void);
bool nvme_handoff_idle(void);
bool nvme_handoff_flush_bounded(u32 nsid, u32 timeout_usec, u32 *stage);

bool nvme_flush(u32 nsid);
bool nvme_read(u32 nsid, u64 lba, void *buffer);
bool nvme_write(u32 nsid, u64 lba, const void *buffer);
bool nvme_read_blocks(u32 nsid, u64 lba, u32 blocks, void *buffer);
bool nvme_write_blocks(u32 nsid, u64 lba, u32 blocks, const void *buffer);
bool nvme_get_namespace_info(u32 nsid, u64 *blocks, u32 *lba_size);

/* Offline native backend interfaces; trusted EL2 owner only, no guest pointers.
 * Serialize all calls under the controller lock with IRQ exclusion. A failed
 * boot/stop pins backing and blocks both frontends until explicit stopped
 * retirement. generation is the broker's fresh, nonzero controller epoch. */
enum nvme_lifecycle_stage {
    NVME_STAGE_INVALID = 0, NVME_STAGE_ALLOCATE = 1, NVME_STAGE_RTKIT_BOOT = 2,
    NVME_STAGE_BOOT_STATUS = 3, NVME_STAGE_DISABLE = 4, NVME_STAGE_ENABLE = 5,
    NVME_STAGE_READY = 6, NVME_STAGE_DRAIN = 7, NVME_STAGE_SHUTDOWN = 8,
    NVME_STAGE_RTKIT_STOP = 9, NVME_STAGE_STOPPED = 10,
    NVME_STAGE_RTKIT_AP_SEND = 11, NVME_STAGE_RTKIT_AP_ACK = 12,
    NVME_STAGE_RTKIT_IOP_SEND = 13, NVME_STAGE_RTKIT_IOP_ACK = 14,
    NVME_STAGE_RTKIT_CPU_STOP = 15, NVME_STAGE_SOFT_RESET = 16,
    NVME_STAGE_FLUSH_SUBMIT = 17, NVME_STAGE_FLUSH_COMPLETION = 18,
    NVME_STAGE_FLUSH_DONE = 19,
};
bool nvme_native_init(u64 generation, u32 timeout_usec, u32 *stage);
int nvme_native_submit(u64 generation, unsigned queue, unsigned tag,
                        const struct nvme_command *cmd, u32 direction);
int nvme_native_poll(u64 generation, unsigned queue, struct nvme_completion *cqe);
bool nvme_native_queue_backing(unsigned queue, u64 *sq_pa, u64 *cq_pa);
u64 nvme_native_generation(void);
/* Read-only EL2-lifetime count, saturating, only nonzero native TCB status reads. */
u64 nvme_native_tcb_inval_failures(void);
/* Lock-free peek: a posted, unconsumed CQE on the native admin or I/O queue. */
bool nvme_native_cq_pending(void);
/* Caller first freezes recovery admission, drains and proves actual Flush.
 * This function independently refuses outstanding engine tags. It performs no
 * flush or queue deletion. Success means SHST done, RDY clear, RTKit AP/IOP
 * acknowledgements and ASC CPU start bit clear, with all allocations retained.
 * Use retire_stopped only after external mapping/TCB lifetime is also retired. */
bool nvme_stop_bounded(u32 timeout_usec, u32 *stage);
bool nvme_retire_stopped(void);

#endif
