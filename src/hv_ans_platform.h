/* SPDX-License-Identifier: MIT */
#ifndef HV_ANS_PLATFORM_H
#define HV_ANS_PLATFORM_H
#include "types.h"
struct exc_info;
/* Called from hv_tick: re-signal a completion whose interrupt was lost (ANS006-R7). */
void hv_ans_tick(void);
bool hv_map_ans_broker(u64 expected_base, u64 expected_size, u32 expected_irq, u32 abi_major);
/* ANS008: one-shot pre-guest WRITE LBA window; without it the broker stays read-only. */
bool hv_ans_write_window(u64 first, u64 count, u32 abi_major);
bool hv_ans_try_handle_dabort(struct exc_info *, bool *matched);
bool hv_ans_mapping_begin(u64 ipa, u64 size);
void hv_ans_mapping_end(void);
void hv_ans_note_physical_irq(u32 irq);
#endif
