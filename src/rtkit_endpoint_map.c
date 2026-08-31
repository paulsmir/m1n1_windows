/* SPDX-License-Identifier: MIT */

#include "rtkit_endpoint_map.h"

void rtkit_endpoint_map_init(struct rtkit_endpoint_map *map)
{
    for (unsigned int i = 0; i < RTKIT_ENDPOINT_WORDS; i++)
        map->words[i] = 0;
}

bool rtkit_endpoint_map_add_chunk(struct rtkit_endpoint_map *map, uint32_t base,
                                  uint32_t bitmap)
{
    if (base >= RTKIT_ENDPOINT_WORDS)
        return false;

    map->words[base] |= bitmap;
    return true;
}

bool rtkit_endpoint_map_contains(const struct rtkit_endpoint_map *map,
                                 uint8_t ep)
{
    uint32_t word = ep / RTKIT_ENDPOINT_WORD_BITS;
    uint32_t bit = ep % RTKIT_ENDPOINT_WORD_BITS;

    return (map->words[word] & (1U << bit)) != 0;
}
