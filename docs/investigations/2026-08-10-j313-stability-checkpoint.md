# J313 eight-core stability checkpoint

## Scope

This checkpoint records the first eight-core configuration that boots installed
Windows reliably enough for sustained interactive diagnosis.  It is not a claim
that platform stability is complete: quiet Windows still exhibits occasional
whole-desktop pauses of approximately 20 seconds, after which execution resumes.

The checkpoint combines the following changes:

- autonomous Apple CPU-frequency initialization before guest entry, with the
  efficiency and performance clusters placed in their validated maximum states;
- direct lowering of Windows' ordinary `BRK` exception path without taking the
  hypervisor-wide lock;
- a dedicated NVMe lock so synchronous Apple ANS work does not hold the global
  hypervisor lock while other vCPUs need timer or IPI service;
- per-CPU FIQ handling for guest timer and local IPI sources before the legacy
  global slow path;
- a T8103 secondary EL2 housekeeping tick reduction from 5000 Hz to 100 Hz;
- manifest-controlled runtime diagnostics, so `debug=off` does not print timer,
  SGI, NVMe, or watchdog telemetry;
- lockless per-CPU watchdog snapshots for monitor builds.

Each policy or state transition has a host-side unit test.  The freestanding
Stage 0 and Stage 1 images are also built as part of the checkpoint validation.

## Hardware evidence

The diagnostic image was tested on a J313 MacBook Air with all four efficiency
and four performance cores online.

- The autonomous preflight recorded the efficiency cluster at P-state 5 and the
  performance cluster moving from P-state 1 to P-state 7 before Windows entry.
- Every secondary CPU (1 through 7) consumed its PSCI entry and entered the
  guest.
- Reducing only the secondary EL2 tick lowered observed EL2 FIQ traffic from
  roughly 40,000 FIQ/s to roughly 11,500 FIQ/s.  The hypervisor's own tick
  component fell to approximately 5,000/s, as expected from one 5000 Hz service
  CPU plus seven 100 Hz secondary CPUs.
- Firmware startup became substantially faster and repeated immediate
  `CLOCK_WATCHDOG_TIMEOUT` failures stopped during the tested workload.
- A quiet `display=physical, debug=off` image reproduced the remaining pause,
  proving that USB logging and the virtual display are not its root cause.
- During a monitor-mode pause, the service CPU continued processing EL2 FIQs
  and Windows later answered SSH again.  The event is therefore not a complete
  machine reset or a permanent USB transport loss.
- Windows reported approximately 10,000--18,000 interrupts/s during the live
  sample.  CPU0--CPU3 carried most scheduled work while CPU4--CPU7 were often
  idle; one sample placed CPU1 at about 34 percent interrupt time.  These are
  investigation leads, not yet a root-cause conclusion.

The validated local artifacts were intentionally not committed:

| Profile | Manifest flags | SHA-256 |
| --- | ---: | --- |
| physical + monitor | `0x11` | `856836456e80a072d7d54a56e499a4291a639034836b8eac31f676e0a894bd3c` |
| physical + debug off | `0x01` | `ca38d4fd01ea6cedcd024d8342c6a5a7cef132cc184a4522435039a6136fd616` |

Both images contain the same Stage 0 binary, Stage 1 binary, Mu firmware, guest
layout, and CPU/timer changes.  Only the validated launch-profile flags differ.

## Current limitation

The remaining symptom is a temporary global user-visible pause: the physical
mouse cursor and desktop stop updating for about 20 seconds and then recover.
It occurs with the quiet physical-only profile, so monitor output and USB
framebuffer streaming can increase latency but cannot explain the production
symptom.

No watchdog, timer, SGI, ACPI, or scheduler hypothesis is considered proven
until a capture spans the start and end of one pause with synchronized EL2 and
Windows timestamps.

## Iteration workflow

Further experiments use assisted boot to avoid rewriting the ESP after every
change:

1. Preserve this checkpoint and its standalone artifact hashes.
2. Build Stage 1 and Mu from one recorded source revision.
3. Chainload that m1n1 build over the proxy endpoint.
4. Start the matching Mu image with physical display and the minimum diagnostic
   mode required by the current hypothesis.
5. Reproduce one pause and retain the complete host log under `.local/`.
6. Change one observable variable, run the host tests, rebuild, and repeat.
7. For every confirmed fix, pack the same Stage 0, Stage 1, Mu, layout, and
   profile into a standalone image and cold-boot it before merging.

This keeps assisted and standalone launch semantics identical.  Assisted mode
is only the faster transport for experiments; it is not a separate product
configuration.

## Completion gate

The remaining stability work is complete only after the quiet standalone image
passes all of the following on hardware:

- 20 consecutive cold boots;
- 60 minutes of mixed CPU, NVMe, USB, network, window-movement, SSH, and RDP
  activity;
- no `CLOCK_WATCHDOG_TIMEOUT`, `IPI_WATCHDOG_TIMEOUT`, CPU-progress loss, or
  pause longer than two seconds;
- normal shutdown followed by a boot that does not enter recovery;
- equivalent behavior with runtime diagnostics enabled and disabled.
