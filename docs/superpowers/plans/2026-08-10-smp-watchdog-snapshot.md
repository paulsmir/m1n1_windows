# SMP Watchdog Snapshot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve the guest timebase on every secondary CPU and capture a low-overhead, per-CPU postmortem that identifies the exact timer/IPI state behind Windows watchdog crashes.

**Architecture:** `hv_secondary_info` carries the boot CPU's virtual-counter offset to every secondary before its timers are enabled. A small standalone snapshot module implements a seqlock-style per-CPU record; `hv_exc.c` periodically publishes hardware and vGIC state without locks or formatting, while the existing one-shot bugcheck detector prints frozen copies only after Windows has already entered a stop path.

**Tech Stack:** C11 atomics, AArch64 EL2 system registers, m1n1 vGIC, shell-based host tests, cross-compiled m1n1 firmware.

## Global Constraints

- Do not acquire `bhl`, allocate memory, walk guest memory, or print from the snapshot publisher.
- Release builds must not emit periodic diagnostic UART traffic.
- Preserve the existing non-zero time-stealing offset; never force `CNTVOFF_EL2` to zero.
- Do not rewrite or discard unrelated uncommitted work in the feature worktree.
- Do not add `Co-Authored-By`, Codex, Claude, or session metadata to commits.

---

### Task 1: Lock-free per-CPU snapshot record

**Files:**
- Create: `src/hv_watchdog_snapshot.h`
- Create: `src/hv_watchdog_snapshot.c`
- Create: `tests/hv_watchdog_snapshot_test.c`
- Modify: `tests/run_host_tests.sh`
- Modify: `Makefile`

**Interfaces:**
- Produces: `struct hv_watchdog_cpu_sample`, `struct hv_watchdog_cpu_record`, `hv_watchdog_snapshot_publish(record, sample)`, and `hv_watchdog_snapshot_read(record, out)`.
- Consumes: only fixed-width integer and boolean types; host tests define `HV_WATCHDOG_SNAPSHOT_HOST_TEST`.

- [ ] **Step 1: Write the failing test**

```c
static void test_publish_exposes_one_complete_generation(void)
{
    struct hv_watchdog_cpu_record record = {0};
    struct hv_watchdog_cpu_sample in = {
        .cpu = 5, .pc = 0xfffff802ee276998ULL,
        .cntpct = 0x589afab31ULL, .sgi_iar = 18288, .sgi_eoi = 18287,
    };
    struct hv_watchdog_cpu_sample out = {0};

    hv_watchdog_snapshot_publish(&record, &in);
    assert(hv_watchdog_snapshot_read(&record, &out));
    assert(out.cpu == 5);
    assert(out.pc == 0xfffff802ee276998ULL);
    assert(out.cntpct == 0x589afab31ULL);
    assert(out.sgi_iar == 18288);
    assert(out.sgi_eoi == 18287);
}
```

Add a second test that sets the record sequence to an odd value and asserts that `hv_watchdog_snapshot_read` returns `false`, preventing a torn postmortem from being printed.

- [ ] **Step 2: Run the focused test and verify RED**

Run: `tests/run_host_tests.sh hv_watchdog_snapshot_test`

Expected: FAIL because `hv_watchdog_snapshot.h` and its API do not exist.

- [ ] **Step 3: Implement the minimal record**

Use an odd/even generation protocol: increment to odd, copy the sample, publish the next even generation with release ordering; readers acquire the first and second generation and accept the copy only when both are equal and even.

- [ ] **Step 4: Run the focused and complete host suites**

Run: `tests/run_host_tests.sh hv_watchdog_snapshot_test`

Expected: PASS.

Run: `tests/run_host_tests.sh`

Expected: every host test passes.

### Task 2: Preserve the virtual counter offset on secondary CPUs

**Files:**
- Modify: `src/hv.c` in `struct hv_secondary_info_t`, `hv_start`, and `hv_init_secondary`

**Interfaces:**
- Consumes: the existing `hv_secondary_info` boot-CPU register snapshot.
- Produces: every secondary enters the guest with the same `CNTVOFF_EL2` value as the boot CPU.

- [ ] **Step 1: Extend the Task 1 sample test with the timebase invariant**

Publish a literal non-zero `.cntvoff = 0x123456789abcdef0ULL` and assert the reader returns exactly that value. This protects the diagnostic path from silently truncating or zeroing the value used to verify secondary initialization.

- [ ] **Step 2: Run the focused test and verify RED**

Run: `tests/run_host_tests.sh hv_watchdog_snapshot_test`

Expected: FAIL because `struct hv_watchdog_cpu_sample` does not yet carry `cntvoff`.

- [ ] **Step 3: Implement the secondary register copy**

Add `u64 cntvoff` to `hv_secondary_info_t`, set it with `mrs(CNTVOFF_EL2)` in `hv_start`, and restore it with `msr(CNTVOFF_EL2, info->cntvoff)` in `hv_init_secondary` before `hv_arm_tick(true)`. Follow the register writes with the existing final `isb` boundary before guest entry.

- [ ] **Step 4: Re-run host tests**

Run: `tests/run_host_tests.sh hv_watchdog_snapshot_test`

Expected: PASS with the full 64-bit literal preserved.

### Task 3: Capture and print decisive per-CPU watchdog state

**Files:**
- Modify: `src/hv_exc.c`
- Modify: `src/hv.c`
- Modify: `src/hv.h`
- Test: `tests/hv_watchdog_snapshot_test.c`

**Interfaces:**
- Produces: `hv_watchdog_snapshot_tick(struct exc_info *ctx)` and `hv_watchdog_snapshot_dump(void)`.
- Consumes: per-CPU timer registers, virtual-GIC list registers, SGI diagnostic counters, last IAR/EOI metadata, and Task 1's publisher/reader.

- [ ] **Step 1: Add a failing cadence test**

Add `hv_watchdog_snapshot_due(u64 fiq_count)` to the Task 1 API and test literal boundaries:

```c
assert(!hv_watchdog_snapshot_due(4095));
assert(hv_watchdog_snapshot_due(4096));
assert(!hv_watchdog_snapshot_due(4097));
```

- [ ] **Step 2: Run the focused test and verify RED**

Run: `tests/run_host_tests.sh hv_watchdog_snapshot_test`

Expected: FAIL because `hv_watchdog_snapshot_due` is undefined.

- [ ] **Step 3: Implement sparse owner-only capture**

Return true from `hv_watchdog_snapshot_due` only when `(per_cpu_sample_tick & 0xfff) == 0`. Add an owner-only per-CPU sample counter rather than reusing the current global `hv_fiq_count`. In `hv_exc.c`, each CPU then records: CPU ID, ELR/SPSR read directly with `hv_get_elr()` and `hv_get_spsr()`, `CNTPCT_EL0`, `CNTVCT_EL0`, `CNTVOFF_EL2`, physical and virtual timer control/comparator values, SGI queue/IAR/EOI counts, pending mask, last IAR/EOI INTIDs, and all implemented ICH list registers. Invoke it from both the secondary fast FIQ return path and the serialized FIQ path. Do not read ELR/SPSR from fast-path `struct exc_info` fields because that path does not populate them with `hv_get_context()`.

- [ ] **Step 4: Print only after a latched Windows bugcheck**

Declare `hv_watchdog_snapshot_dump` in `hv.h`. Call it immediately after the existing `HV BUGCHECK` line in `hv_percpu_diag_tick`. The dumper reads stable records and prints one bounded line per CPU; it does not run during normal release execution.

- [ ] **Step 5: Run all host tests**

Run: `tests/run_host_tests.sh`

Expected: every host test passes with no warnings.

### Task 4: Firmware build and artifact verification

**Files:**
- No additional source files unless required by the existing build scripts.

**Interfaces:**
- Consumes: completed Tasks 1-3.
- Produces: release and diagnostic m1n1 binaries suitable for the existing standalone image builder.

- [ ] **Step 1: Build the diagnostic firmware**

Run:

```bash
make clean
PATH="$(brew --prefix rustup)/bin:$PATH" make -j8 EXTRA_CFLAGS=-DM1N1_STAGE0
mkdir -p .local/smp-watchdog-snapshot
cp build/m1n1.bin .local/smp-watchdog-snapshot/m1n1-stage0-debug.bin
make clean
PATH="$(brew --prefix rustup)/bin:$PATH" make -j8 EXTRA_CFLAGS=-DM1N1_STAGE1
cp build/m1n1.bin .local/smp-watchdog-snapshot/m1n1-stage1-debug.bin
cp build/m1n1.macho .local/smp-watchdog-snapshot/m1n1-debug.macho
```

Expected: clean compile and link; `hv_watchdog_snapshot.o` is linked.

- [ ] **Step 2: Build the release firmware**

Run:

```bash
make clean
PATH="$(brew --prefix rustup)/bin:$PATH" make -j8 RELEASE=1 EXTRA_CFLAGS=-DM1N1_STAGE0
cp build/m1n1.bin .local/smp-watchdog-snapshot/m1n1-stage0-release.bin
make clean
PATH="$(brew --prefix rustup)/bin:$PATH" make -j8 RELEASE=1 EXTRA_CFLAGS=-DM1N1_STAGE1
cp build/m1n1.bin .local/smp-watchdog-snapshot/m1n1-stage1-release.bin
cp build/m1n1.macho .local/smp-watchdog-snapshot/m1n1-release.macho
```

Expected: clean compile and link; normal boot contains no periodic snapshot prints.

- [ ] **Step 3: Inspect the final diff**

Run: `git diff --check`

Expected: no whitespace errors.

Run: `git status --short`

Expected: only the intended watchdog/timebase work plus the pre-existing acknowledged feature changes.

- [ ] **Step 4: Produce one diagnostic standalone image**

Run:

```bash
python3 /Users/pavel/public_windows/tools/pack_boot.py \
  --stage0-m1n1 .local/smp-watchdog-snapshot/m1n1-stage0-debug.bin \
  --stage1-m1n1 .local/smp-watchdog-snapshot/m1n1-stage1-debug.bin \
  --firmware /Users/pavel/public_windows/mu/Build/MacBookAirMid2020-AARCH64/DEBUG_CLANGPDB/FV/J313MACBOOKAIRMID2020_EFI.fd \
  --layout /Users/pavel/public_windows/config/j313-guest-layout.json \
  --output .local/smp-watchdog-snapshot/boot-physical-monitor.bin \
  --display physical \
  --debug monitor \
  --source-commit "$(git rev-parse HEAD)" \
  --compiler "$("$(brew --prefix llvm)/bin/clang" --version | sed -n '1p')"
shasum -a 256 .local/smp-watchdog-snapshot/boot-physical-monitor.bin
```

Do not install or reboot the Air until the user explicitly confirms the hardware-test step.
