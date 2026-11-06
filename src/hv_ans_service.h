/* SPDX-License-Identifier: MIT */
#ifndef HV_ANS_SERVICE_H
#define HV_ANS_SERVICE_H
#include "hv_ans_broker.h"
#include "hv_ans_prp.h"
#include "ans_queue.h"
/* All callbacks are trusted EL2 adapters, invoked with the service/controller
 * lock and IRQ exclusion held. None may copy READ/WRITE payload. */
struct ans_service_ops {
    bool (*freeze_recovery)(void *);
    bool (*drain_recovery)(void *);
    bool (*flush_recovery)(void *);
    bool (*stop)(void *, uint32_t *stage);
    bool (*retire)(void *);
    /* Called only after stop+retire. Success proves the old frontend is hidden,
     * not that native controller/admin queues or guest DMA are ready. */
    bool (*hide_recovery)(void *);
    bool (*boot_admin)(void *, uint64_t generation, uint32_t *stage);
    bool (*queue_backing)(void *, unsigned queue, uint64_t *sq, uint64_t *cq);
    int (*submit)(void *, uint64_t generation, unsigned queue, unsigned tag,
                  const struct nvme_command *, uint32_t direction);
    int (*poll)(void *, uint64_t generation, unsigned queue, struct nvme_completion *);
    bool (*shadow_list)(void *, unsigned tag, const uint64_t *pages, unsigned count,
                        uint64_t *host_pa);
    void (*release_tag)(void *, unsigned tag);
};
struct ans_service {
    struct ans_broker broker;
    struct ans_service_ops ops;
    struct ans_memory_ops memory;
    void *ctx;
    void *memory_ctx;
    uint64_t namespace_blocks, pending;
    struct ans_write_window write_window;
    uint64_t submitted, completed, reaped, rejected;
    uint32_t last_stage;
    struct ans_telemetry telemetry;
    uint32_t transfer_bytes[ANS_QUEUE_DEPTH];
    bool io_cq, io_sq, io_cq_pending, io_sq_pending, handoff_started, admin_started;
    uint8_t opcode[ANS_QUEUE_DEPTH];
    struct ans_handoff_receipt handoff;
};
/* Never reinitialize a live instance. Read-only unless a write window is set. */
bool ans_service_init(struct ans_service *, const struct ans_service_ops *, void *,
                      const struct ans_memory_ops *, void *, uint64_t namespace_blocks);
/* ANS008: fix the WRITE LBA window once, before handoff (host launcher, pre-guest).
 * Rejects count==0, a range outside the namespace, a second call, or any call after
 * handoff started. Success replaces READ_ONLY with WRITE_WINDOW in the features. */
bool ans_service_set_write_window(struct ans_service *, uint64_t first, uint64_t count);
void ans_service_handoff(struct ans_service *, uint64_t sequence);
void ans_service_execute(struct ans_service *, const struct ans_broker_request *,
                         struct ans_broker_response *);
int ans_service_drain(struct ans_service *);
/* Record one rejected transaction without changing admission/ownership. */
void ans_service_record_rejection(struct ans_service *, uint32_t result);
void ans_service_telemetry_snapshot(const struct ans_service *, struct ans_telemetry *);
#endif
