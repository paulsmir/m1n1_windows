# Shared ANS2 queue engine (offline only)

This is the bounded phase 2/4 extraction used by the existing `nvme.c` recovery
wrappers. It is not a native controller boot, stop, handoff, IRQ, or DMA mapping
implementation. No hardware was contacted or launched for this change.

## Inspected sources and contract

- This worktree's `src/nvme.c`: 64-entry SQ/CQ/TCB arrays, synchronous tag 0,
  command/TCB publication, phase, invalidation, CQ acknowledgement, controller
  lifetime and recovery read/write/flush/identify wrappers.
- Pinned Linux `drivers/nvme/host/apple.c` at
  `77cb8f24c2381a8abb7272d7bbdec548d6426a8a`, read via the parent worktree's
  `.local/ans-native/references/apple.c`: `apple_nvme_submit_cmd_t8103`,
  `apple_nvmmu_inval`, `apple_nvme_poll_cq`, tagsets and TCB definitions.
  This GPL implementation was used as observable protocol evidence only; the
  MIT implementation extracts the existing MIT m1n1 operations.
- Parent `documentation/NATIVE_ANS_STORAGE.md` and
  `documentation/plans/J313_NATIVE_ANS_REQUEST.md`, phases 2, 4 and queue/test
  requirements; `src/hv_ans_broker.h` and `src/hv_ans_prp.h` integration contracts.
  The parent audit records Asahi DT, Mu ACPI/EBS, Windows/StorPort references,
  and preserved assisted recovery identities; none is a new hardware receipt.

The engine owns command/TCB metadata publication, one shared 64-tag namespace,
per-CQ head/phase and successful invalidation-to-ack ordering. The controller
owner supplies two trusted coherent backing sets, serializes every call with
IRQ exclusion, implements MMIO/barriers, and owns power, RTKit, SART, interrupts,
DMA mappings, timeouts, payload backing, reset proof and recovery. Broker and
Windows request lifecycle remain outside this engine. There is no allocation,
MMIO literal, guest pointer, payload memcpy or payload dereference in the engine.

Native policy reserves tags `[0, admin_reserved)` for admin and the rest for I/O;
use two reserved slots for pinned Linux's AQ_DEPTH=2. Its admin blk-mq depth is
one, which is different from its two reserved tag slots. Recovery explicitly
permits tag 0 on either queue only when no request is outstanding. Command CID
is rewritten to the authorized 8-bit tag and reserved high byte zero.

## Source differences remain explicit

- Recovery supplies the command opcode and DMA flags 3 to TCB construction.
  Linux T8103 supplies opcode 0 and directional flags (or zero without PRP1).
  The engine takes these fields explicitly; native authorization must select
  them from its verified contract. It does not guess native semantics.
- Recovery keeps NVMMU_TCB_STAT at `0x29120` and logs nonzero status as before.
  Linux uses `0x28120`. The recovery callback is explicitly advisory and returns
  true; it MUST NOT be used as a native invalidation proof. The native callback
  must resolve the register conflict and return a truthful receipt. A false
  callback result faults the engine, retains the tag, and leaves CQ unacknowledged.
- Linux documents missed interrupts if submission interleaves between
  invalidation and CQ acknowledgement. All engine operations require the same
  controller lock/IRQ exclusion; release occurs after ack. This extraction does
  not install an IRQ handler or supply that lock to a future native caller.
- CQ ownership is checked via a volatile phase load, followed by DMA read barrier
  and the completion snapshot. Reserved DWORD2 is not treated as PCI NVMe SQID.
- A recovery timeout or invalid CQ now faults the engine instead of permitting a
  later request to overwrite tag 0 while prior DMA may remain active. Successful
  read/write/flush/identify behavior, allocation sizes and MMIO choices are kept.
  Recovery teardown still has its existing controller/RTKit failure-handling
  limitations. Queue extraction is not proof that teardown safely stopped DMA.

## APIs and limits

`ans_queue_init` accepts a fresh engine and stopped controller backing; it must
never be used to bypass reset of a live native instance. `ans_queue_submit`
publishes authorized host-address metadata under an explicit generation, queue
and tag. `ans_queue_poll` is nonblocking, returns one original wire completion,
and releases the engine tag only after invalidation/ack. The broker must still
retain its completed tag and PRP shadow until its own completion/reap contract
allows reuse. `ans_queue_fault` blocks submission while retaining ownership.
`ans_queue_reset` requires actual DMA-stop proof, advances a nonwrapping
controller generation and clears CQs while hardware is stopped.

A CQE carries no generation or request sequence. Generation checks reject stale
API calls and reset clears old CQ bytes; they cannot distinguish a late duplicate
CQE after the same tag was legitimately reused. Correct hardware completion
semantics and reset excluding late DMA are prerequisites. Likewise init and its
trusted pointers are an internal controller API, not validation of guest queues.
Native queue registration, truthful bounded RTKit/controller stop, source-conflict
resolution and connection to broker state are remaining integration gates.

## Falsifiable checkpoint and verification

The smallest current checkpoint is offline: mocked callbacks must observe SQE/TCB
before publish, phase before CQ snapshot, invalidation before ack and tag release
after ack. Out-of-order completion, shared-tag collision, wrap, stale callers,
duplicate/wrong-CQ/out-of-range completion, invalidation failure and stopped-only
reset are deterministic invariants. Tests use deliberately non-dereferenceable
PRP addresses to catch any attempted payload access. Mutated barrier/tag ordering
must fail. Recovery vNVMe tests and native ARM64 object compilation remain gates.

No hardware checkpoint is authorized here. When the full architecture is ready,
the parent plan's first checkpoint remains ANS001 (independent WinPE, ANS0 unbound,
virtual NVMe absent, no disk command), with the preserved ordinary G2 pair as
recovery. A queue-only extraction does not satisfy that launch gate.
