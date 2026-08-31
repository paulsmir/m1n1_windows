#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "../src/dcp_iomfb_owner_lifecycle.h"

struct fixture {
    unsigned int calls;
    bool system_ok;
    bool iomfb_ok;
};

static bool start_system(void *opaque)
{
    struct fixture *fixture = opaque;
    assert(fixture->calls == 0);
    fixture->calls = 1;
    return fixture->system_ok;
}

static bool start_iomfb(void *opaque)
{
    struct fixture *fixture = opaque;
    assert(fixture->calls == 1);
    fixture->calls = 2;
    return fixture->iomfb_ok;
}

int main(void)
{
    struct fixture fixture = {.system_ok = true, .iomfb_ok = true};
    assert(dcp_iomfb_owner_start_ordered(&fixture, start_system, start_iomfb));
    assert(fixture.calls == 2);

    fixture = (struct fixture){.system_ok = false, .iomfb_ok = true};
    assert(!dcp_iomfb_owner_start_ordered(&fixture, start_system, start_iomfb));
    assert(fixture.calls == 1);

    fixture = (struct fixture){.system_ok = true, .iomfb_ok = false};
    assert(!dcp_iomfb_owner_start_ordered(&fixture, start_system, start_iomfb));
    assert(fixture.calls == 2);

    puts("dcp_iomfb_owner_lifecycle_test: ok");
    return 0;
}
