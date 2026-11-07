# ANS001 handoff is not native DMA readiness

The assisted launch descriptor in `run_uefi.py` publishes DART_TABLES with
flags=UNOBSERVED and size=0. `hv_launch_j313_publish_descriptor()` and
`hv_launch_j313_capture_base()` preserve that marker. It is not evidence of an
empty reservation. The production `ans_memory_context_valid()` correctly rejects
this descriptor, and the native platform's `boot()` and `translate()` remain
closed when that memory qualification fails.

The earlier platform `hide()` returned the memory qualification result after
recovery had already been stopped, retired and hidden. Consequently a descriptor
that correctly admitted no guest DMA also prevented the independent, package-free
ANS001 handoff receipt from completing. The corrected boundary keeps the actual
stop/retirement/hide prerequisites intact and records memory qualification
separately. Handoff READY means that the old recovery frontend is retired and
native logical ownership may be claimed. It does not mean that the native
controller, admin queues, namespace, or DMA are ready.

**Source-derived full V1 DMA readiness is NOT established.** This correction
admits package-free ANS001 enumeration and QUERY/CLAIM only. It does not admit
ANS003, ANS004, Identify payloads, filesystem reads, or any other native data
transfer. REGISTER_QUEUES remains unsuccessful while the memory policy rejects
the descriptor, and the test checks that the actual native backend initialization
callback is never entered. No UNOBSERVED flag is cleared and no zero reservation
is fabricated.

## Source audit and unresolved ownership proof

Inspected current sources:

- `run_uefi.py`: actual descriptor construction and copied guest RAM geometry.
- `hv_launch_j313.c`: descriptor publication and snapshot collection; region flags
  are retained rather than resolved by the platform reader.
- `hv_ans_memory.c` and `hv_ans_platform.c`: reservation qualification, stage-2
  authorization, handoff adapter, and fail-closed native boot/translation.
- `dart.c`: L1 allocation in `dart_init()`, inherited valid TTBR roots when locked
  or keep_pts, L2 allocation in `dart_get_l2()`, and inherited/preallocated
  `pt-region-*` handling in `dart_setup_pt_region()`.
- `dcp.c`: retained DCP/display/PIODMA DART owners and preallocated table regions.
- `hv_vgic.c`: xHCI DART initialization can happen later, when the guest first
  enables or programs the controller.
- `heapblock.c`, `dlmalloc/malloc_config.h`, and `HV.load_raw()`: allocator origins
  and guest stage-2 mapping. Heap allocation alone does not inventory inherited
  DART page tables.

A tracker of only currently opened DARTs would not prove completeness. A later
inherited DART import could reveal a page that ANS had already DMA-authorized;
excluding it only from future submissions cannot repair those earlier grants.
A valid future DMA proof must cover retained/preallocated table owners before the
first guest grant and preserve exclusion for all later allocations/imports during
the full grant lifetime. No live powered-off DART census or unobserved hardware
register access was introduced to guess that proof. No DART, allocator, GPU,
Windows-driver, or shared diagnostic-descriptor behavior was changed.

## Deterministic regression and checkpoint

`tests/hv_ans_handoff_memory_test.py` evaluates only the actual runner descriptor
assignment with explicit synthetic boot-observation inputs, passes the resulting
632-byte descriptor through the production C publication/capture pipeline, and
uses extracted production `prepare_memory()`, `hide()`, and `boot()` callbacks.
The original code fails the expected successful package-free handoff assertion;
the correction passes while retaining the exact UNOBSERVED marker and proving
native boot and full-range translation remain denied. A failed real stop still
prevents handoff and hiding. The existing memory, service and MMIO tests remain
required.

The next hardware checkpoint remains ANS001 unbound enumeration only, after a
new exact backend build and independent root preregistration. This source fix is
not a hardware result and cannot turn an old image into a validated candidate.
Recovery remains the unchanged hardware-validated recovery artifacts identified
by the root experiment ledger. No hardware or firmware-image build accompanied
this correction.
