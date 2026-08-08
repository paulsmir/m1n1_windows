#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/hv_launch_transport.h"

struct test_sink {
    unsigned char
        bytes[sizeof(struct hv_launch_frame_header) + sizeof(struct hv_contract_snapshot)];
    size_t used;
    size_t limit;
};

static size_t partial_sink(void *opaque, const void *data, size_t size)
{
    struct test_sink *sink = opaque;
    size_t take = size < sink->limit ? size : sink->limit;

    memcpy(sink->bytes + sink->used, data, take);
    sink->used += take;
    return take;
}

int main(void)
{
    struct hv_contract_snapshot snapshot = {0};
    struct hv_launch_transport transport;
    struct test_sink sink = {.limit = 7};
    enum hv_launch_transport_result result;

    snapshot.header.magic = HV_CONTRACT_MAGIC;
    snapshot.header.version = HV_CONTRACT_VERSION;
    snapshot.header.checkpoint = HV_CONTRACT_PRE_GUEST;
    snapshot.header.sequence = 4;
    assert(hv_contract_finalize(&snapshot));
    assert(hv_launch_transport_begin(&transport, &snapshot));

    do {
        result = hv_launch_transport_pump(&transport, partial_sink, &sink);
    } while (result == HV_LAUNCH_TRANSPORT_PENDING);

    assert(result == HV_LAUNCH_TRANSPORT_COMPLETE);
    assert(sink.used == sizeof(struct hv_launch_frame_header) + sizeof(snapshot));
    assert(!memcmp(sink.bytes, HV_LAUNCH_FRAME_MAGIC, HV_LAUNCH_FRAME_MAGIC_SIZE));
    assert(
        !memcmp(sink.bytes + sizeof(struct hv_launch_frame_header), &snapshot, sizeof(snapshot)));
    assert(hv_launch_transport_pump(&transport, partial_sink, &sink) ==
           HV_LAUNCH_TRANSPORT_COMPLETE);
    assert(sink.used == sizeof(struct hv_launch_frame_header) + sizeof(snapshot));

    puts("hv_launch_transport_test: ok");
    return 0;
}
