#include <assert.h>
#include <stdio.h>

#include "../src/hv_watchdog_snapshot.h"

static void test_publish_exposes_one_complete_generation(void)
{
    struct hv_watchdog_cpu_record record = {0};
    struct hv_watchdog_cpu_sample in = {
        .cpu = 5,
        .pc = 0xfffff802ee276998ULL,
        .cntpct = 0x589afab31ULL,
        .cntvoff = 0x123456789abcdef0ULL,
        .hcr = 0x80000081ULL,
        .ich_hcr = 0x1405ULL,
        .ich_vmcr = 0xf8000003ULL,
        .isr = 0x40ULL,
        .sgi_iar = 18288,
        .sgi_eoi = 18287,
    };
    struct hv_watchdog_cpu_sample out = {0};

    hv_watchdog_snapshot_publish(&record, &in);
    assert(hv_watchdog_snapshot_read(&record, &out));
    assert(out.cpu == 5);
    assert(out.pc == 0xfffff802ee276998ULL);
    assert(out.cntpct == 0x589afab31ULL);
    assert(out.cntvoff == 0x123456789abcdef0ULL);
    assert(out.hcr == 0x80000081ULL);
    assert(out.ich_hcr == 0x1405ULL);
    assert(out.ich_vmcr == 0xf8000003ULL);
    assert(out.isr == 0x40ULL);
    assert(out.sgi_iar == 18288);
    assert(out.sgi_eoi == 18287);
}

static void test_reader_rejects_an_in_progress_generation(void)
{
    struct hv_watchdog_cpu_record record = {.sequence = 1};
    struct hv_watchdog_cpu_sample out = {0};

    assert(!hv_watchdog_snapshot_read(&record, &out));
}

static void test_sparse_capture_cadence(void)
{
    assert(!hv_watchdog_snapshot_due(63));
    assert(hv_watchdog_snapshot_due(64));
    assert(!hv_watchdog_snapshot_due(65));
}

static void test_periodic_dump_cadence_handles_skipped_ticks(void)
{
    assert(!hv_watchdog_snapshot_dump_due(999999, 0, 1000000));
    assert(hv_watchdog_snapshot_dump_due(1000000, 0, 1000000));
    assert(!hv_watchdog_snapshot_dump_due(1999999, 1000000, 1000000));
    assert(hv_watchdog_snapshot_dump_due(2000001, 1000000, 1000000));
}

int main(void)
{
    test_publish_exposes_one_complete_generation();
    test_reader_rejects_an_in_progress_generation();
    test_sparse_capture_cadence();
    test_periodic_dump_cadence_handles_skipped_ticks();
    puts("hv_watchdog_snapshot_test: ok");
    return 0;
}
