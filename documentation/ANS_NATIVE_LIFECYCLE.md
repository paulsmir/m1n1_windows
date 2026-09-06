# Native ANS controller lifecycle primitives — offline

These APIs are not wired to a launcher, Mu, a hypercall, guest MMIO, IRQ routing,
or a StorPort miniport. No hardware was contacted for this change. Recovery and
native policies share the existing MIT controller/queue implementation; no GPL
implementation was copied.

## Source evidence and ownership

Inspected this worktree's `src/{nvme,ans_queue,rtkit,asc,pmgr}.{c,h}`; the existing
`ANS_QUEUE_ENGINE.md`; and pinned Linux
`77cb8f24c2381a8abb7272d7bbdec548d6426a8a`:

- `drivers/nvme/host/apple.c`, available in the parent ANS worktree at
  `.local/ans-native/references/apple.c`: `apple_nvme_submit_cmd_t8103`,
  `apple_nvmmu_inval`, `apple_nvme_disable`, `apple_nvme_reset_work`,
  `apple_nvme_reg_write32`, `apple_nvme_queue_alloc`.
- `drivers/nvme/host/core.c`, `nvme_enable_ctrl` and `nvme_disable_ctrl`, read from
  https://raw.githubusercontent.com/AsahiLinux/linux/77cb8f24c2381a8abb7272d7bbdec548d6426a8a/drivers/nvme/host/core.c
- `include/linux/nvme.h`, CAP/CC fields and NVM queue-entry sizes, read from
  https://raw.githubusercontent.com/AsahiLinux/linux/77cb8f24c2381a8abb7272d7bbdec548d6426a8a/include/linux/nvme.h

`nvme_begin_native_handoff()` reserves native ownership under the caller's
controller lock only while physical recovery is idle. This reservation closes
all legacy init/read/write/flush/Identify/shutdown/ensure-shutdown entry points
immediately and persists through stopped retirement; only a fresh m1n1 BSS world
restores recovery admission. `nvme_native_init` requires this reservation.
`nvme_handoff_idle()` observes the reserved but physically recovery-owned idle
state without reopening the public compatibility interfaces.

`nvme_handoff_flush_bounded(nsid, timeout_usec, stage)` submits a real Flush via
recovery tag0 and historical TCB policy. Its callback mode suppresses legacy
RTKit polling and advisory invalidation printing. One deadline covers submission
and strict allocation-free single-message RTKit/CQ polling. Only an actual
successful CQE yields FLUSH_DONE; timeout/error faults the engine, preserving any
outstanding tag. Advisory recovery TCB status remains advisory, not a DMA-stop
receipt; the subsequent bounded physical stop remains mandatory.

`nvme_recovery_idle()` is a trusted, controller-lock observation of physical
recovery readiness: no native reservation, initialized backend, recovery mode, no fault/quarantine and
no outstanding engine tag. All compatibility read/write/flush/Identify wrappers
use it before changing PRP metadata or allocating Identify backing. Native owns
only the explicit-tag APIs; compatibility wrappers cannot become a second frontend, including the gap between
logical handoff and physical native initialization.

The higher EL2 owner freezes frontend admission, serializes controller/IRQ access,
validates every guest command and DMA range, supplies the authoritative generation,
and retains guest mappings/shadows until completion/reap or actual stopped
retirement. This backend owns physical SQ/CQ/TCB backing, RTKit/SART allocation,
controller registers, command publication, and lifecycle observations. Hardware
performs payload DMA. Firmware/Windows architecture is unchanged by these helpers.

## Native and recovery policies

Native init receives `(generation, timeout_usec, stage_out)`, refuses an existing
backend, allocates both queue backing sets, requires ASC RUN clear and a successful
existing PMGR reset (`ANS`, with `ANS2` fallback), then performs bounded RTKit boot
and controller initialization. This follows the pinned Linux requirement that a
fresh boot after clean shutdown starts with CPU RUN clear and a soft reset. PMGR
reset's return value is checked; its register writes are not themselves a hardware
reset receipt. The subsequent RTKit handshake, boot magic, CAP checks and RDY are
separate required observations. PMGR lookup/logging is in native initialization,
never in the pre-EBS bounded stop gate.

Native init enables **admin only**. It does not send CreateCQ/CreateSQ, Identify,
Read, Write or Flush. `nvme_native_queue_backing` provides trusted EL2 SQ/CQ
addresses; only the broker may use them to replace queue-creation pointers.
Backend pointers and registers are never exported to guest code by these APIs.
The allocated admin CQ remains 64 entries to match the current shared engine's
head/phase geometry and m1n1 backing contract; native tags reserve slots 0 and 1.
Linux uses a two-entry admin CQ. Do not change AQA to two entries without also
making the engine's admin CQ wrap depth two.

Native CC is written explicitly while disabled: IOSQES=6, IOCQES=4, MPS=0,
CSS=NVM, AMS=round-robin, SHN=none, CRIME=0, then EN=1. CAP must advertise NVM,
4 KiB support (MPSMIN=0), at least 64 entries and the fixed doorbell stride used
here. An all-ones or fatal CSTS is rejected. No inherited UEFI CC fields are used.
Pinned Linux's `APPLE_NVME_IOSQES=7` is **not** the T8103 CC value: it is the older
T8015 non-LSQ backing stride. T8103 uses 64-byte SQEs; generic `nvme_enable_ctrl`
programs CC IOSQES6/IOCQES4 and apple's register callback forwards it unchanged.
Native programs the Linux linear-enable value and pending count64; recovery keeps
its existing read/modify/write, PRP-null-check bit, and pending count63.

Native submission uses TCB opcode0, exact direction0/1/2, and low16 CDW12 NLB.
The SQE retains the full CDW12 including FUA. Recovery keeps the command opcode,
bidirectional flags3 and historical full CDW12 TCB length. This prevents native
FUA/control bits from incorrectly extending TCB length.

`nvme_native_tcb_inval_failures()` is a read-only EL2-lifetime cumulative counter.
It increments (saturating) only when the native invalidation callback actually
reads nonzero TCB status; recovery status, refused submissions, getter reads and
retirement do not increment/reset it.

Native invalidation reads status at source-derived 0x28120 and treats nonzero as
failure, leaving the tag pinned/CQ unacknowledged. Recovery retains advisory
0x29120 exactly. Linux also treats its status as advisory; the stricter native
zero requirement is an explicit fail-closed policy, not new hardware validation.

For broker-patched CreateCQ/CreateSQ, the guest request has no payload but the
hardware SQE has a nonzero trusted queue PRP1. Pass TCB TO_DEVICE (2), consistent
with the pinned odd-opcode admin direction rule. Identify uses FROM_DEVICE (1);
commands with no PRP use NONE (0). The backend rejects combined directions and a
contradictory PRP1/direction pair; it is not a substitute for broker command checks.

## Stop and retirement

`nvme_stop_bounded(timeout_usec, stage_out)` requires the higher owner to have
already frozen admission, drained requests and observed a real successful Flush.
It independently rejects outstanding engine tags. One absolute physical-counter
deadline covers normal SHN/SHST completion, EN clear/RDY clear, RTKit AP QUIESCED
acknowledgement, IOP SLEEP acknowledgement and ASC CPU RUN clear. It neither
allocates nor logs, and does not depend on firmware timers or interrupts.

The register-wait caller explicitly selects initialization or shutdown dispatch.
`rtkit_poll_boot_once` uses bounded full system dispatch during BOOT_STATUS and
RDY waits: late firmware buffer requests after AP ON are supported, because no
inspected source guarantees they are already finished. `rtkit_poll_system_once`
is the strict stop/drain variant. Both consume at most one message per call.
Shutdown
accepts management power acknowledgements and bounded acknowledgements of known
post-boot logging messages. Buffer requests, crash messages and unsupported
traffic fail closed instead of allocating, printing, parsing a firmware crash
buffer, or hiding an unbounded drain loop. Power waits require an acknowledgement
after sending the request, rather than accepting cached matching power fields.
All power operations and dispatch must be serialized; RTKit supplies no wire
transaction sequence to distinguish an unsolicited duplicate acknowledgement.

A bounded buffer-reply failure retains the newly allocated/mapped RTKit buffer
and its ownership record; a repeated request cannot overwrite that record. Only
successful stop followed by retirement frees it. The legacy unbounded handler's
failure behavior is preserved. Actual-handler tests include allocation, reply
timeout, stop and retirement; they no longer mock away the buffer handler.

Every failure leaves the engine faulted and keeps all backing. A failed bounded
stop also blocks legacy `nvme_shutdown` and `nvme_init` from freeing/reusing the
backend. A native boot failure after backend establishment similarly retains it.
No failure path re-enables the controller or returns a fake DMA-stop receipt.
A failed graceful stop with outstanding tags is not a forced abort/reset API.

Success retains every allocation and produces a stable STOPPED receipt.
`nvme_retire_stopped()` is separate and requires higher-layer mapping/shadow
retirement before freeing queues, RTKit/SART state and ASC bookkeeping. The native reservation remains set across retirement. A new
native instance uses the broker's next authoritative generation, not a private
reset-to-one public epoch. Existing recovery `nvme_init`/`nvme_shutdown` continue
their historical policy when no native/bounded-stop transition owns the backend.
Existing `rtkit_sleep` return semantics are unchanged and are never used as proof.

The bounded RTKit boot wrapper reuses the existing handshake with deadline-aware
mailbox send/receive and message-drain loops; an endless endpoint-map or system
message stream cannot evade the deadline. Normal APIs delegate to the old behavior
when bounded mode is off. Boot may allocate bounded protocol metadata; it is not
the allocation-free Mu stop operation. Blocking hardware bus failures and host
allocator execution are outside the software mailbox timeout guarantee.

## Deterministic verification and remaining checkpoint

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Missing power acknowledgements,
unbounded mailbox/system-message storms, stale cached power accepted as a receipt,
allocation during stop, premature backing release after failure, wrong generation,
accidental I/O queue creation during admin-only boot, inherited CC fields, native
TCB length polluted by FUA, or native/recovery invalidation policy crossing.

Run these from the isolated m1n1 worktree:

- `python3 tests/rtkit_native_lifecycle_test.py`
- `python3 tests/nvme_native_lifecycle_test.py`
- `sh tests/run_host_tests.sh ans_queue_test`
- `python3 tests/nvme_prp_quarantine_test.py`
- Existing `rtkit_power_ack_contract_test.py` and `rtkit_tracekit_contract_test.py`.

The lifecycle tests extract actual production function bodies and use synthetic
mailbox/register callbacks. They compile with ASan/UBSan and warning errors.
The FUA and native CC checks were observed RED against the old paths and GREEN
after the targeted fixes. Only changed ARM64 objects were compiled locally; no
hardware image was built, signed, installed or launched.

The next checkpoint is still offline integration with the broker's authoritative
owner/generation and the status-returning Mu gate. Physical IRQ routing, mapping
pins, driver/package admission and the saved recovery launch contract remain
separate gates. Only later explicit authorization permits ANS001, the independent
USB/RAM WinPE enumeration-only checkpoint; its recovery is the preserved G2 pair.
No native storage success or hardware safety qualification is claimed here.
