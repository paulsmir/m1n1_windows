/* SPDX-License-Identifier: MIT */

#include "hv_nvme_fast_path.h"

bool hv_nvme_bar_contains(u64 base, u64 size, u64 ipa)
{
    return base && size && ipa >= base && ipa - base < size;
}
