/* SPDX-License-Identifier: MIT */

#include "hv_guest_cpu_state.h"

#define HACR_TRAP_CTRR        (1ULL << 14)
#define HACR_TRAP_IPI         (1ULL << 16)
#define HACR_TRAP_SERROR_INFO (1ULL << 48)
#define HACR_TRAP_EHID        (1ULL << 49)
#define HACR_TRAP_HID         (1ULL << 50)
#define HACR_TRAP_ACC         (1ULL << 52)
#define HACR_TRAP_PMUV3       (1ULL << 56)
#define HACR_TRAP_PM          (1ULL << 57)

#define MDCR_TDE   (1ULL << 8)
#define MDCR_TDA   (1ULL << 9)
#define MDCR_TDOSA (1ULL << 10)
#define MDCR_TDRA  (1ULL << 11)

#define MDSCR_MDE       (1ULL << 15)
#define AMX_CONFIG_EL1  (1ULL << 62)
#define ACTLR_EN_MDSB   (1ULL << 12)

struct hv_guest_cpu_state hv_guest_cpu_state_prepare(uint64_t current_amx_config,
                                                      uint64_t current_actlr)
{
    return (struct hv_guest_cpu_state){
        .hacr = HACR_TRAP_CTRR | HACR_TRAP_IPI | HACR_TRAP_SERROR_INFO |
                HACR_TRAP_EHID | HACR_TRAP_HID | HACR_TRAP_ACC |
                HACR_TRAP_PMUV3 | HACR_TRAP_PM,
        .mdcr = MDCR_TDE | MDCR_TDA | MDCR_TDOSA | MDCR_TDRA,
        .mdscr = MDSCR_MDE,
        .amx_config = current_amx_config | AMX_CONFIG_EL1,
        .apvmkeylo = 0x4e7672476f6e6147ULL,
        .apvmkeyhi = 0x697665596f755570ULL,
        .apsts = 1,
        .actlr = current_actlr | ACTLR_EN_MDSB,
    };
}
