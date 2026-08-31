/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdio.h>

#include "../src/rtkit_endpoint_map.h"

static void test_empty_map_rejects_every_endpoint(void)
{
    struct rtkit_endpoint_map map;

    rtkit_endpoint_map_init(&map);
    for (unsigned int ep = 0; ep < RTKIT_ENDPOINT_COUNT; ep++)
        assert(!rtkit_endpoint_map_contains(&map, (uint8_t)ep));
}

static void test_chunks_preserve_system_and_application_endpoints(void)
{
    struct rtkit_endpoint_map map;

    rtkit_endpoint_map_init(&map);
    assert(rtkit_endpoint_map_add_chunk(&map, 0, (1U << 1) | (1U << 8)));
    assert(rtkit_endpoint_map_add_chunk(&map, 1, (1U << 23)));

    assert(rtkit_endpoint_map_contains(&map, 1));
    assert(rtkit_endpoint_map_contains(&map, 8));
    assert(rtkit_endpoint_map_contains(&map, 0x37));
    assert(!rtkit_endpoint_map_contains(&map, 0x36));
}

static void test_out_of_range_chunk_fails_closed_without_mutation(void)
{
    struct rtkit_endpoint_map map;

    rtkit_endpoint_map_init(&map);
    assert(!rtkit_endpoint_map_add_chunk(&map, RTKIT_ENDPOINT_WORDS, 1));
    assert(!rtkit_endpoint_map_contains(&map, 0));
}

int main(void)
{
    test_empty_map_rejects_every_endpoint();
    test_chunks_preserve_system_and_application_endpoints();
    test_out_of_range_chunk_fails_closed_without_mutation();
    puts("rtkit_endpoint_map_test: ok");
    return 0;
}
