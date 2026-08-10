# Windows SMP watchdog root-cause design

## Evidence

Windows crash dumps show both `CLOCK_WATCHDOG_TIMEOUT (0x101)` and
`IPI_WATCHDOG_TIMEOUT (0x1db)`. The affected logical processor varies, so this
is not isolated to one Firestorm or Icestorm core.

The last verbose hypervisor capture before a `0x101` shows that the affected
processor accepted SGI 0: its virtual-GIC IAR count advanced and the SGI LR was
Active. Its EOI count did not advance. The dump places that processor in
`KiIpiProcessRequests`. Therefore the remaining failure occurs after virtual
IPI delivery, not before it.

`CNTVOFF_EL2` must still be initialized consistently before every secondary
enters the guest. That register only offsets `CNTVCT_EL0`; it does not change
the physical `CNTPCT_EL0` used by the observed Windows QPC path. Copying
`CNTVOFF_EL2` is an architectural correctness fix, but is not sufficient proof
of the watchdog root cause.

## Design

1. Snapshot the boot CPU's `CNTVOFF_EL2` in `hv_secondary_info` and restore it
   on every secondary before timers are enabled or the guest is entered.
2. Add a fixed-size, per-CPU watchdog snapshot in normal RAM. Hot exception
   paths may update only their own cache-line-aligned record with atomic or
   owner-only stores; they must not format text, allocate, acquire `bhl`, or
   perform guest-memory walks.
3. Record the minimum state needed to distinguish the remaining causes:
   physical and virtual counter values, timer control/comparator state, SGI
   queued/IAR/EOI counters, live LR state, guest PC/SPSR, and the last EL2 path
   marker.
4. When the existing bugcheck detector observes `0x101` or `0x1db`, print one
   frozen snapshot for all CPUs from the interruptible CPU. Normal release
   execution remains free of periodic UART logging.
5. Use the resulting capture to choose the functional fix:
   - a counter delta isolated to a CPU or cluster requires counter
     virtualization/calibration;
   - an Active SGI with a non-progressing guest PC requires correction of the
     virtual interrupt/EOI state machine;
   - a processor stopped in EL2 identifies the exact fast or serialized path
     that must be removed from the vCPU's critical path.

## Rejected shortcuts

- Setting `CNTVOFF_EL2` to zero discards the existing time-stealing contract.
- Treating `CNTVOFF_EL2` as a fix for `CNTPCT_EL0` is architecturally invalid.
- Adding frequent UART prints to FIQ or SGI paths changes timing and has already
  caused false hangs.
- Reverting to one CPU hides the SMP defect and is not a solution.

## Validation

Host tests cover the secondary timebase policy and snapshot state transitions.
The firmware must build in both release and diagnostic configurations. One
cold boot under the existing Steam/download workload is then sufficient to
either validate the timebase correction or produce the decisive per-CPU
postmortem needed for the next functional patch.
