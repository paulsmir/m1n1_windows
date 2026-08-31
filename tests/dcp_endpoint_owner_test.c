#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "../src/dcp_endpoint_owner.h"

struct fixture {
    int shutdown_result;
    unsigned int shutdown_calls;
    unsigned int release_calls;
};

static int shutdown_endpoint(void *opaque)
{
    struct fixture *fixture = opaque;
    fixture->shutdown_calls++;
    return fixture->shutdown_result;
}

static void release_owner(void *opaque)
{
    struct fixture *fixture = opaque;
    fixture->release_calls++;
}

int main(void)
{
    struct fixture fixture = {0};
    assert(dcp_endpoint_owner_shutdown(&fixture, shutdown_endpoint,
                                       release_owner) == 0);
    assert(fixture.shutdown_calls == 1 && fixture.release_calls == 1);

    fixture = (struct fixture){.shutdown_result = -7};
    assert(dcp_endpoint_owner_shutdown(&fixture, shutdown_endpoint,
                                       release_owner) == -7);
    assert(fixture.shutdown_calls == 1 && fixture.release_calls == 0);

    assert(dcp_endpoint_owner_shutdown(NULL, shutdown_endpoint,
                                       release_owner) == 0);

    puts("dcp_endpoint_owner_test: ok");
    return 0;
}
