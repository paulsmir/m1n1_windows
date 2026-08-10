# Standalone CPU-frequency initialization design

## Evidence

The J313 standalone monitor boot reaches Windows with all eight virtual CPUs
alive and with balanced SGI delivery.  A Counter-Strike 1.6 run remained
responsive to SSH, but rendered at roughly 0.3 FPS.  Windows reports eight
cores and eight logical processors, while `Win32_Processor` reports both
`CurrentClockSpeed` and `MaxClockSpeed` as 23 MHz.

The standalone launch path goes directly from the embedded manifest to the
hypervisor and Mu firmware.  It never calls `cpufreq_init()`.  In upstream
m1n1, that function is called for a kernel payload and through an explicit
proxy command, but not by the autonomous hypervisor path.  Consequently the
J313 ECPU and PCPU clusters are not deterministically moved from their boot
P-states to m1n1's established defaults before Windows starts.

Windows also exposes no physical video controller in this build.  Only the
RDP indirect display adapter is present, so the game result is not a valid GPU
benchmark.  CPU-frequency initialization and GPU acceleration remain separate
problems.

## Selected design

1. Add a small standalone CPU-preflight boundary that invokes m1n1's existing
   `cpufreq_init()` exactly once before autonomous guest preparation begins.
2. Treat initialization failure as a blocking standalone preflight failure.
   Do not enter Mu or Windows with an unknown cluster state.
3. Log the J313 ECPU and PCPU `CLUSTER_PSTATE` values before and after
   initialization in monitor builds so a hardware run proves that the desired
   P-states were applied.
4. Keep assisted/proxy boot behavior unchanged.  The change is scoped to the
   autonomous path that currently omits the initialization.
5. Do not add ACPI CPPC or a Windows frequency driver in this patch.  Dynamic
   frequency management is a later design; this patch only restores the
   deterministic m1n1 initialization contract used before handing off a
   kernel payload.

## Failure handling

If `cpufreq_init()` returns an error, print a standalone preflight failure,
flush the diagnostic console when available, and return
`HV_AUTONOMOUS_BOOT_ATTEMPT_FAILED`.  This avoids a silent low-frequency boot
and leaves the existing recovery/proxy path available.

## Tests

- A host test must fail before implementation because autonomous launch does
  not invoke a CPU-initialization operation.
- The passing test must prove that initialization runs once and precedes the
  launch operation.
- A second test must prove that an initialization error blocks launch and is
  reported as a failed boot attempt.
- Run the focused host test, the complete host-test suite, and both stage0 and
  stage1 firmware builds.
- Hardware validation requires one cold standalone monitor boot.  The log must
  show the before/after ECPU and PCPU P-state values, Windows must reach SSH,
  and a short bounded CPU workload must complete without watchdog reset.

## Out of scope

- Apple AGX acceleration and Direct3D support.
- ACPI CPPC and dynamic Windows-controlled P-state selection.
- Changes to SGI priority or timer delivery.
- Hiding the defect by reducing the guest CPU count.
