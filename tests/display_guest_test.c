#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/display_guest.h"

struct fake_display {
    unsigned maps;
    unsigned presents;
    unsigned unmaps;
    uint64_t mapped_iova;
    bool present_ok;
    uint32_t owner_swap_id;
    int owner_latch_results[4];
    unsigned owner_latch_count;
    unsigned owner_latch_index;
    unsigned waits;
};

static uint64_t fake_map(void *opaque, uint64_t base, uint64_t size)
{
    struct fake_display *fake = opaque;
    assert(base == UINT64_C(0x85f000000));
    assert(size == UINT64_C(0x3e8000));
    fake->maps++;
    return fake->mapped_iova;
}

static bool fake_present(void *opaque, uint64_t iova, uint32_t width, uint32_t height,
                         uint32_t stride)
{
    struct fake_display *fake = opaque;
    assert(iova == fake->mapped_iova);
    assert(width == 1280);
    assert(height == 800);
    assert(stride == 5120);
    fake->presents++;
    return fake->present_ok;
}

static void fake_unmap(void *opaque, uint64_t iova, uint64_t size)
{
    struct fake_display *fake = opaque;
    assert(iova == fake->mapped_iova);
    assert(size == UINT64_C(0x3e8000));
    fake->unmaps++;
}

static uint32_t fake_owner_present_begin(void *opaque, uint64_t iova,
                                         uint32_t width, uint32_t height,
                                         uint32_t stride)
{
    struct fake_display *fake = opaque;
    assert(iova == fake->mapped_iova);
    assert(width == 1280 && height == 800 && stride == 5120);
    fake->presents++;
    return fake->owner_swap_id;
}

static int fake_owner_latch_poll(void *opaque, uint32_t expected_swap_id)
{
    struct fake_display *fake = opaque;
    assert(expected_swap_id == fake->owner_swap_id);
    assert(fake->owner_latch_index < fake->owner_latch_count);
    return fake->owner_latch_results[fake->owner_latch_index++];
}

static void fake_owner_wait(void *opaque)
{
    ((struct fake_display *)opaque)->waits++;
}

int main(void)
{
    const uint64_t base = UINT64_C(0x85f000000);
    const uint64_t size = UINT64_C(0x3e8000);

    assert(display_guest_validate(base, size, 1280, 800, 5120, 32));
    assert(!display_guest_validate(0, size, 1280, 800, 5120, 32));
    assert(!display_guest_validate(base + 1, size, 1280, 800, 5120, 32));
    assert(!display_guest_validate(base, 0, 1280, 800, 5120, 32));
    assert(!display_guest_validate(base, size, 0, 800, 5120, 32));
    assert(!display_guest_validate(base, size, 1280, 0, 5120, 32));
    assert(!display_guest_validate(base, size, 1280, 800, 5119, 32));
    assert(!display_guest_validate(base, size, 1280, 800, 5120, 30));
    assert(!display_guest_validate(base, size - 1, 1280, 800, 5120, 32));
    assert(!display_guest_validate(UINT64_MAX - 0x3fff, size, 1280, 800, 5120, 32));
    assert(!display_guest_validate(base, UINT64_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, 32));

    struct display_guest_rect destination;
    assert(display_guest_fit(1280, 800, 2560, 1600, &destination));
    assert(destination.width == 2560 && destination.height == 1600);
    assert(destination.x == 0 && destination.y == 0);
    assert(display_guest_fit(1280, 720, 2560, 1600, &destination));
    assert(destination.width == 2560 && destination.height == 1440);
    assert(destination.x == 0 && destination.y == 80);
    assert(!display_guest_fit(0, 800, 2560, 1600, &destination));
    assert(!display_guest_fit(1280, 800, 0, 1600, &destination));
    assert(!display_guest_fit(1280, 800, 2560, 1600, NULL));

    const struct display_guest_ops ops = {
        .map = fake_map,
        .present = fake_present,
        .unmap = fake_unmap,
    };
    uint64_t iova = 0;
    struct fake_display fake = {.mapped_iova = UINT64_C(0x12340000), .present_ok = true};
    assert(display_guest_prepare(base, size, 1280, 800, 5120, 32, &ops, &fake, &iova));
    assert(iova == fake.mapped_iova);
    assert(fake.maps == 1 && fake.presents == 1 && fake.unmaps == 0);

    fake = (struct fake_display){.mapped_iova = 0, .present_ok = true};
    assert(!display_guest_prepare(base, size, 1280, 800, 5120, 32, &ops, &fake, &iova));
    assert(fake.maps == 1 && fake.presents == 0 && fake.unmaps == 0);

    fake = (struct fake_display){.mapped_iova = UINT64_C(0x12340000), .present_ok = false};
    assert(!display_guest_prepare(base, size, 1280, 800, 5120, 32, &ops, &fake, &iova));
    assert(fake.maps == 1 && fake.presents == 1 && fake.unmaps == 1);

    fake = (struct fake_display){.mapped_iova = UINT64_C(0x12340000), .present_ok = true};
    assert(!display_guest_prepare(base, size - 1, 1280, 800, 5120, 32, &ops, &fake, &iova));
    assert(fake.maps == 0 && fake.presents == 0 && fake.unmaps == 0);

    const struct display_guest_owner_ops owner_ops = {
        .map = fake_map,
        .present_begin = fake_owner_present_begin,
        .latch_poll = fake_owner_latch_poll,
        .wait = fake_owner_wait,
    };
    fake = (struct fake_display){
        .mapped_iova = UINT64_C(0x12340000),
        .owner_swap_id = 41,
        .owner_latch_results = {0, 0, 1},
        .owner_latch_count = 3,
    };
    assert(display_guest_prepare_owner(base, size, 1280, 800, 5120, 32,
                                       &owner_ops, &fake, 4, &iova) ==
           DISPLAY_GUEST_OWNER_LATCHED);
    assert(iova == fake.mapped_iova);
    assert(fake.maps == 1 && fake.presents == 1 && fake.waits == 2);

    fake = (struct fake_display){
        .mapped_iova = UINT64_C(0x12340000),
        .owner_swap_id = 42,
        .owner_latch_results = {0, 0},
        .owner_latch_count = 2,
    };
    assert(display_guest_prepare_owner(base, size, 1280, 800, 5120, 32,
                                       &owner_ops, &fake, 2, &iova) ==
           DISPLAY_GUEST_OWNER_UNCERTAIN);
    assert(iova == fake.mapped_iova);
    assert(fake.unmaps == 0);

    fake = (struct fake_display){
        .mapped_iova = UINT64_C(0x12340000),
        .owner_swap_id = 0,
    };
    assert(display_guest_prepare_owner(base, size, 1280, 800, 5120, 32,
                                       &owner_ops, &fake, 2, &iova) ==
           DISPLAY_GUEST_OWNER_UNCERTAIN);
    assert(iova == fake.mapped_iova && fake.unmaps == 0);

    fake = (struct fake_display){.mapped_iova = UINT64_C(0x12340000)};
    assert(display_guest_prepare_owner(base, size - 1, 1280, 800, 5120, 32,
                                       &owner_ops, &fake, 2, &iova) ==
           DISPLAY_GUEST_OWNER_REJECTED);
    assert(iova == 0 && fake.maps == 0 && fake.presents == 0);

    puts("display_guest_test: ok");
    return 0;
}
