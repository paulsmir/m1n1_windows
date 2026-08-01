#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/ringbuffer.h"

int main(void)
{
    ringbuffer_t *ring = ringbuffer_alloc(4);
    const u8 input[] = {1, 2, 3, 4};
    u8 output[2] = {0};

    assert(ring != NULL);
    assert(ringbuffer_get_used(ring) == 0);
    assert(ringbuffer_get_free(ring) == 3);
    assert(ringbuffer_write(input, sizeof(input), ring) == 3);
    assert(ringbuffer_get_used(ring) == 3);
    assert(ringbuffer_get_free(ring) == 0);
    assert(ringbuffer_read(output, 1, ring) == 1);
    assert(output[0] == 1);
    assert(ringbuffer_get_free(ring) == 1);
    assert(ringbuffer_write(input + 3, 1, ring) == 1);
    assert(ringbuffer_get_free(ring) == 0);

    ringbuffer_free(ring);
    puts("ringbuffer_test: ok");
    return 0;
}
