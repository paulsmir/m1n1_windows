# Standalone CPU-Frequency Initialization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Initialize the J313 ECPU and PCPU clusters deterministically before every autonomous Windows launch and block launch when initialization fails.

**Architecture:** Add a generic autonomous `PLATFORM` stage immediately after manifest/profile validation, so both quiet and monitor standalone paths cross the same boundary.  The J313 runtime implements that stage with m1n1's existing `cpufreq_init()` and emits raw before/after cluster P-state evidence in monitor mode.

**Tech Stack:** C11, m1n1 PMGR/cluster P-state support, existing shell host-test harness, AArch64 cross-build.

## Global Constraints

- Do not change the assisted/proxy launch path.
- Do not change SGI priority, virtual timers, CPU topology, or guest CPU count.
- Do not add ACPI CPPC or GPU support in this patch.
- Preserve all unrelated dirty work in the worktree.
- Do not add `Co-Authored-By`, Codex, Claude, or session metadata to commits.

---

### Task 1: Add the autonomous platform-preflight stage

**Files:**
- Modify: `src/hv_autonomous.h`
- Modify: `src/hv_autonomous_runtime.c`
- Modify: `tests/hv_autonomous_stage_test.c`

**Interfaces:**
- Produces: `HV_AUTONOMOUS_STAGE_PLATFORM`, ordered after `HV_AUTONOMOUS_STAGE_VALIDATE` and before `HV_AUTONOMOUS_STAGE_DECOMPRESS`.
- Consumes: the existing `hv_autonomous_prepare_with_ops()` callback pipeline.

- [x] **Step 1: Write the failing ordering and failure tests**

Extend `tests/hv_autonomous_stage_test.c` so the success case explicitly
asserts:

```c
assert(recorder.seen[0] == HV_AUTONOMOUS_STAGE_VALIDATE);
assert(recorder.seen[1] == HV_AUTONOMOUS_STAGE_PLATFORM);
assert(recorder.seen[2] == HV_AUTONOMOUS_STAGE_DECOMPRESS);
```

Add a platform-failure case and prove that decompression is never reached:

```c
recorder = (struct recorder){.fail_at = HV_AUTONOMOUS_STAGE_PLATFORM};
assert(hv_autonomous_prepare_with_ops(&payload, &status, &ops, &recorder) ==
       HV_AUTONOMOUS_RESULT_STAGE_FAILED);
assert(recorder.count == 2);
assert(status.stage == HV_AUTONOMOUS_STAGE_PLATFORM);
```

- [x] **Step 2: Run the focused test and verify RED**

Run: `tests/run_host_tests.sh hv_autonomous_stage_test`

Expected: compilation fails because `HV_AUTONOMOUS_STAGE_PLATFORM` does not exist.

- [x] **Step 3: Implement the minimal pipeline stage**

Insert `HV_AUTONOMOUS_STAGE_PLATFORM` after `HV_AUTONOMOUS_STAGE_VALIDATE` in
`src/hv_autonomous.h`.  Add one `runtime_stage` callback to the static callback
array in `hv_autonomous_prepare()`.  Initially make the runtime switch return
`true` for this stage so the focused host test exercises only ordering and
failure propagation.

- [x] **Step 4: Run the focused test and verify GREEN**

Run: `tests/run_host_tests.sh hv_autonomous_stage_test`

Expected: `hv_autonomous_stage_test: ok`.

### Task 2: Initialize and report cluster P-states

**Files:**
- Create: `src/cpufreq_state.h`
- Create: `src/cpufreq_state.c`
- Modify: `src/cpufreq.h`
- Modify: `src/cpufreq.c`
- Modify: `src/hv_autonomous_runtime.c`
- Create: `tests/cpufreq_state_test.c`
- Modify: `tests/run_host_tests.sh`
- Modify: `Makefile`

**Interfaces:**
- Produces: `u32 cpufreq_decode_pstate(u32 soc_id, u64 value)` as a pure register decoder and `void cpufreq_print_state(const char *phase)` for firmware diagnostics.
- Consumes: existing `int cpufreq_init(void)` and the new autonomous platform stage.

- [x] **Step 1: Write the failing P-state decode test**

Create `tests/cpufreq_state_test.c` and verify that
the T8103 desired P-state is decoded from bits `[4:0]`:

```c
assert(cpufreq_decode_pstate(T8103, 0) == 0);
assert(cpufreq_decode_pstate(T8103, 5) == 5);
assert(cpufreq_decode_pstate(T8103, 7 | (1ULL << 25)) == 7);
```

- [x] **Step 2: Run the focused test and verify RED**

Run: `tests/run_host_tests.sh cpufreq_state_test`

Expected: compilation fails because the public decoder and test target do not exist.

- [x] **Step 3: Implement the minimal observable initialization**

Move the existing pure register-format selection into
`cpufreq_state.c` as `cpufreq_decode_pstate(soc_id, value)` and make
`cpufreq.c` consume it.  Add
`cpufreq_print_state(phase)`, which prints each configured cluster's name, raw
`CLUSTER_PSTATE` register, decoded current P-state, and configured default
P-state.  In `HV_AUTONOMOUS_STAGE_PLATFORM`:

```c
if (runtime->profile.monitor)
    cpufreq_print_state("before");
int result = cpufreq_init();
if (runtime->profile.monitor)
    cpufreq_print_state("after");
if (result) {
    printf("PREFLIGHT FAIL checkpoint=PLATFORM reason=cpufreq\n");
    iodev_console_kick();
    return false;
}
printf("PREFLIGHT PASS checkpoint=PLATFORM component=cpufreq\n");
iodev_console_kick();
return true;
```

- [x] **Step 4: Run focused and complete host tests**

Run: `tests/run_host_tests.sh cpufreq_state_test`

Expected: `cpufreq_state_test: ok`.

Run: `tests/run_host_tests.sh hv_autonomous_stage_test`

Expected: `hv_autonomous_stage_test: ok`.

Run: `make host-tests`

Expected: all host tests pass.

### Task 3: Build and validate the monitor artifact

**Files:**
- Build outputs only; do not commit generated binaries.

**Interfaces:**
- Consumes: the existing J313 stage0/stage1 build and standalone packer.
- Produces: one physical-display monitor standalone image with manifest flags `0x11`.

- [x] **Step 1: Build both m1n1 stages**

Run the repository's existing stage0 and stage1 build commands used by the
current monitor artifact.  Both builds must exit zero.

- [x] **Step 2: Pack and inspect the standalone image**

Pack `J313_EFI_8core.fd` into a monitor-profile standalone image.  Parse it
with `standalone_image.py` and verify manifest flags `0x11`, the expected Mu
uncompressed size, and a valid CRC.

- [ ] **Step 3: Hardware validation gate**

Copy the image to the Air but do not install it until the current Windows run
has been shut down cleanly.  After one cold boot, require all of:

- `cpufreq` before/after records for both `ECPU` and `PCPU`;
- `PREFLIGHT PASS checkpoint=PLATFORM component=cpufreq`;
- Windows SSH availability;
- completion of the existing bounded 15-second, eight-worker CPU workload;
- no `CLOCK_WATCHDOG_TIMEOUT`, `IPI_WATCHDOG_TIMEOUT`, or hypervisor reset.

- [ ] **Step 4: Commit only verified source and tests**

Stage the exact source, test, and plan files.  Commit without attribution or
session metadata.  Do not push until the hardware gate passes.
