/* SPDX-License-Identifier: MIT */
#ifndef HV_ANS_PINS_H
#define HV_ANS_PINS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hv_ans_broker_abi.h"
#define ANS_MAX_PIN_RANGES (ANS_MAX_DATA_PAGES + 1)
struct ans_pin_range { uint64_t ipa, size; };
struct ans_pin_set {
    struct ans_pin_range ranges[ANS_QUEUE_DEPTH][ANS_MAX_PIN_RANGES];
    unsigned counts[ANS_QUEUE_DEPTH];
};
/* Same mutation lock must span validate+add+submit and check+actual hv_map.
 * release is permitted only before publication or after TCB/stop proof. */
bool ans_pin_add(struct ans_pin_set *, unsigned tag, uint64_t ipa, uint64_t size);
bool ans_pin_mapping_allowed(const struct ans_pin_set *, uint64_t ipa, uint64_t size);
void ans_pin_release(struct ans_pin_set *, unsigned tag);
#endif
