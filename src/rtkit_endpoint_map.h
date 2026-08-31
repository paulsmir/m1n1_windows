/* SPDX-License-Identifier: MIT */

#ifndef RTKIT_ENDPOINT_MAP_H
#define RTKIT_ENDPOINT_MAP_H

#include <stdbool.h>
#include <stdint.h>

#define RTKIT_ENDPOINT_COUNT 256U
#define RTKIT_ENDPOINT_WORD_BITS 32U
#define RTKIT_ENDPOINT_WORDS (RTKIT_ENDPOINT_COUNT / RTKIT_ENDPOINT_WORD_BITS)

struct rtkit_endpoint_map {
    uint32_t words[RTKIT_ENDPOINT_WORDS];
};

void rtkit_endpoint_map_init(struct rtkit_endpoint_map *map);
bool rtkit_endpoint_map_add_chunk(struct rtkit_endpoint_map *map, uint32_t base,
                                  uint32_t bitmap);
bool rtkit_endpoint_map_contains(const struct rtkit_endpoint_map *map,
                                 uint8_t ep);

#endif
