/* SPDX-License-Identifier: MIT */
#ifndef HV_ANS_DIRECT_H
#define HV_ANS_DIRECT_H
#include <stdbool.h>
#include <stdint.h>

/* ANS-DIRECT hand-off contract (investigation/ANS_DIRECT_PLAN.md): after EL2's
 * one-time cold boot the guest receives the controller with RTKit running and
 * NVMe disabled, exactly the state Asahi apple.c expects from m1n1/U-Boot. */
#define ANS_DIRECT_CC_EN        UINT32_C(0x1)
#define ANS_DIRECT_CSTS_RDY     UINT32_C(0x1)
#define ANS_DIRECT_CSTS_CFS     UINT32_C(0x2)
#define ANS_DIRECT_CSTS_SHST    UINT32_C(0xc)
#define ANS_DIRECT_SHST_BUSY    UINT32_C(0x4)
/* Broker header reserved word after a direct handoff; the Mu gate validates the
 * header only before its handoff write, and the receipt never carries it. */
#define ANS_DIRECT_HEADER_MARKER UINT32_C(0x54435244) /* "DRCT" */

static inline bool nvme_direct_handoff_state_ok(uint32_t cc, uint32_t csts)
{
    return cc != UINT32_MAX && csts != UINT32_MAX && !(cc & ANS_DIRECT_CC_EN) &&
           !(csts & (ANS_DIRECT_CSTS_RDY | ANS_DIRECT_CSTS_CFS)) &&
           (csts & ANS_DIRECT_CSTS_SHST) != ANS_DIRECT_SHST_BUSY;
}
#endif
