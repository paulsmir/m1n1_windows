/* SPDX-License-Identifier: MIT */

#include "cpufreq_state.h"

#include "soc.h"

#define CLUSTER_PSTATE_DESIRED1          UINT64_C(0x1f)
#define CLUSTER_PSTATE_DESIRED1_S5L8960X UINT64_C(0x1c00000)

bool cpufreq_pstate_supported(uint32_t soc_id)
{
    switch (soc_id) {
        case S5L8960X:
        case T7000:
        case T7001:
        case S8000:
        case S8001:
        case S8003:
        case T8010:
        case T8011:
        case T8012:
        case T8015:
        case T8103:
        case T6000:
        case T6001:
        case T6002:
        case T8112:
        case T6020:
        case T6021:
        case T6022:
        case T6031:
            return true;
        default:
            return false;
    }
}

uint32_t cpufreq_decode_pstate(uint32_t soc_id, uint64_t value)
{
    switch (soc_id) {
        case S5L8960X:
        case T7000:
        case T7001:
            return (value & CLUSTER_PSTATE_DESIRED1_S5L8960X) >> 22;
        default:
            return value & CLUSTER_PSTATE_DESIRED1;
    }
}
