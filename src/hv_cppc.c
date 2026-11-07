/* SPDX-License-Identifier: MIT */

/*
 * ACPI CPPC (_CPC) register page for the Windows guest (T8103).
 *
 * Windows ARM64's inbox processor driver (FxPPM) evaluates per-CPU _CPC/_PSD
 * from the firmware DSDT.  The DSDT points every CPU's Desired Performance,
 * Reference/Delivered Performance Counter and Performance Limited registers
 * into this emulated SystemMemory page.  Performance units are MHz, so
 * Nominal Frequency == Nominal Performance and Windows reports real clocks.
 *
 * A cluster runs at the lowest P-state whose frequency covers the highest
 * desired performance of its CPUs (_PSD HW_ALL: hardware coordinates).  Turbo
 * P-states 13-15 are excluded (Asahi: not usable without CPU deep sleep).
 * The DRAM controller follows the P-cluster exactly like macOS
 * (AppleT8101PMGR::setPerfState): FAST DRAMCFG at or above
 * mcx-fast-pcpu-frequency, iBoot's values below it.
 *
 * Feedback counters: Reference = CNTPCT; Delivered integrates CNTPCT ticks
 * scaled by current/nominal frequency of the CPU's cluster.
 */

#include "hv_cppc.h"
#include "adt.h"
#include "cpufreq.h"
#include "hv.h"
#include "mcc.h"
#include "soc.h"
#include "utils.h"

#define CPPC_CPUS     8
#define CPPC_STRIDE   0x40
#define REG_DESIRED   0x00
#define REG_REFERENCE 0x08
#define REG_DELIVERED 0x10
#define REG_LIMITED   0x18

struct cppc_cluster {
    const u16 *mhz; /* index = P-state, [0] unused */
    int max_pstate;
    int cpufreq_index;
    int pstate;
    u64 delivered_acc; /* in reference ticks x nominal units */
    u64 last_tick;
};

static const u16 t8103_e_mhz[] = {0, 600, 972, 1332, 1704, 2064};
static const u16 t8103_p_mhz[] = {0, 600, 828, 1056, 1284, 1500, 1728, 1956, 2184, 2388, 2592, 2772, 2988};

static struct cppc_cluster clusters[2] = {
    {t8103_e_mhz, 5, 0, 5, 0, 0},
    {t8103_p_mhz, 12, 1, 7, 0, 0},
};

static u32 desired[CPPC_CPUS];
static u32 limited[CPPC_CPUS];
static u32 dram_fast_mhz;
static int dram_fast = -1;
static bool cppc_mapped;
static DECLARE_SPINLOCK(cppc_lock);

static inline struct cppc_cluster *cpu_cluster(int cpu)
{
    return &clusters[cpu < 4 ? 0 : 1];
}

static inline u32 nominal_mhz(struct cppc_cluster *c)
{
    return c->mhz[c->max_pstate];
}

/* Account the time spent at the current P-state before changing it. */
static void cluster_account(struct cppc_cluster *c, u64 now)
{
    c->delivered_acc += (now - c->last_tick) * c->mhz[c->pstate];
    c->last_tick = now;
}

static int pstate_for(struct cppc_cluster *c, u32 want_mhz)
{
    for (int ps = 1; ps <= c->max_pstate; ps++)
        if (c->mhz[ps] >= want_mhz)
            return ps;
    return c->max_pstate;
}

static void cluster_update(int index)
{
    struct cppc_cluster *c = &clusters[index];
    u32 want = 0;
    int first = index ? 4 : 0;

    for (int i = first; i < first + 4; i++)
        want = max(want, desired[i]);
    if (!want)
        return;

    int ps = pstate_for(c, want);
    if (ps == c->pstate)
        return;

    cluster_account(c, mrs(CNTPCT_EL0));
    if (cpufreq_set_cluster_pstate(c->cpufreq_index, ps) == 0)
        c->pstate = ps;

    if (index == 1 && dram_fast_mhz) {
        int fast = c->mhz[c->pstate] >= dram_fast_mhz;
        if (fast != dram_fast && mcc_dramcfg_set_fast(fast) == 0)
            dram_fast = fast;
    }
}

static bool cppc_hook(struct exc_info *ctx, u64 addr, u64 *val, bool write, int width)
{
    UNUSED(ctx);
    u64 off = addr - HV_CPPC_BASE;
    int cpu = off / CPPC_STRIDE;
    u64 reg = off % CPPC_STRIDE;

    if (cpu >= CPPC_CPUS || width < 2)
        return false;

    struct cppc_cluster *c = cpu_cluster(cpu);
    bool ok = true;

    spin_lock(&cppc_lock);
    switch (reg) {
        case REG_DESIRED:
            if (write) {
                desired[cpu] = (u32)*val;
                cluster_update(cpu < 4 ? 0 : 1);
            } else {
                *val = desired[cpu];
            }
            break;
        case REG_REFERENCE:
            if (write)
                break;
            *val = mrs(CNTPCT_EL0);
            break;
        case REG_DELIVERED:
            if (write)
                break;
            {
                u64 now = mrs(CNTPCT_EL0);
                u64 acc = c->delivered_acc + (now - c->last_tick) * c->mhz[c->pstate];
                *val = acc / nominal_mhz(c);
            }
            break;
        case REG_LIMITED:
            if (write)
                limited[cpu] &= ~(u32)*val; /* OSPM clears by writing 1s */
            else
                *val = limited[cpu];
            break;
        default:
            if (!write)
                *val = 0;
            break;
    }
    spin_unlock(&cppc_lock);
    return ok;
}

bool hv_cppc_map(void)
{
    if (cppc_mapped)
        return true;
    if (chip_id != T8103) {
        printf("HV CPPC: chip 0x%x unsupported\n", chip_id);
        return false;
    }

    int node = adt_path_offset(adt, "/arm-io/pmgr");
    u32 fast = 0;
    if (node >= 0 && ADT_GETPROP(adt, node, "mcx-fast-pcpu-frequency", &fast) < 0)
        fast = 0;
    dram_fast_mhz = fast;

    u64 now = mrs(CNTPCT_EL0);
    for (int i = 0; i < 2; i++) {
        int ps = cpufreq_get_cluster_pstate(clusters[i].cpufreq_index);
        if (ps >= 1 && ps <= clusters[i].max_pstate)
            clusters[i].pstate = ps;
        clusters[i].last_tick = now;
    }

    if (hv_map_hook(HV_CPPC_BASE, cppc_hook, HV_CPPC_SIZE) < 0) {
        printf("HV CPPC: hook map failed\n");
        return false;
    }
    cppc_mapped = true;
    printf("HV CPPC: _CPC page at 0x%lx (E P1-5 600-2064 MHz, P P1-12 600-2988 MHz, "
           "DRAMCFG fast >= %u MHz)\n",
           (u64)HV_CPPC_BASE, dram_fast_mhz);
    return true;
}
