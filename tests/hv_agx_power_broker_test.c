#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../src/hv_agx_power_broker.h"

enum call {
    CALL_ASC_ON = 1,
    CALL_SGX_ON,
    CALL_SGX_OFF,
    CALL_ASC_OFF,
};

struct fixture {
    enum call calls[8];
    unsigned count;
    enum call fail_call;
};

static const char *platform_paths[8];
static bool platform_enables[8];
static unsigned platform_count;

int pmgr_adt_power_enable(const char *path)
{
    assert(platform_count < 8);
    platform_paths[platform_count] = path;
    platform_enables[platform_count++] = true;
    return 0;
}

int pmgr_adt_power_disable(const char *path)
{
    assert(platform_count < 8);
    platform_paths[platform_count] = path;
    platform_enables[platform_count++] = false;
    return 0;
}

static bool record(struct fixture *fixture, enum call call)
{
    assert(fixture->count < 8);
    fixture->calls[fixture->count++] = call;
    return fixture->fail_call != call;
}

static bool asc_on(void *opaque)
{
    return record(opaque, CALL_ASC_ON);
}

static bool sgx_on(void *opaque)
{
    return record(opaque, CALL_SGX_ON);
}

static bool sgx_off(void *opaque)
{
    return record(opaque, CALL_SGX_OFF);
}

static bool asc_off(void *opaque)
{
    return record(opaque, CALL_ASC_OFF);
}

static struct hv_agx_power_broker make_broker(struct fixture *fixture)
{
    const struct hv_agx_power_ops ops = {
        .asc_on = asc_on,
        .sgx_on = sgx_on,
        .sgx_off = sgx_off,
        .asc_off = asc_off,
    };
    struct hv_agx_power_broker broker;

    memset(&broker, 0xa5, sizeof(broker));
    hv_agx_power_broker_init(&broker, &ops, fixture);
    return broker;
}

static void test_initial_snapshot_and_query_receipt(void)
{
    struct fixture fixture = {0};
    struct hv_agx_power_broker broker = make_broker(&fixture);
    struct hv_agx_power_snapshot snapshot;

    hv_agx_power_broker_snapshot(&broker, &snapshot);
    assert(snapshot.magic == HV_AGX_POWER_MAGIC);
    assert(snapshot.abi_version == HV_AGX_POWER_ABI_VERSION);
    assert(snapshot.capabilities == HV_AGX_POWER_CAP_FIXED_J313_DOMAINS);
    assert(snapshot.state == HV_AGX_POWER_OFF);
    assert(snapshot.receipt_sequence == 0);

    assert(hv_agx_power_broker_command(&broker, HV_AGX_POWER_CMD_QUERY, 1));
    hv_agx_power_broker_snapshot(&broker, &snapshot);
    assert(snapshot.receipt_sequence == 1);
    assert(snapshot.result == HV_AGX_POWER_RESULT_OK);
    assert(snapshot.state == HV_AGX_POWER_OFF);
    assert(fixture.count == 0);
}

static void test_on_off_order_and_idempotence(void)
{
    struct fixture fixture = {0};
    struct hv_agx_power_broker broker = make_broker(&fixture);
    struct hv_agx_power_snapshot snapshot;

    assert(hv_agx_power_broker_command(&broker, HV_AGX_POWER_CMD_ON, 1));
    assert(fixture.count == 2);
    assert(fixture.calls[0] == CALL_ASC_ON);
    assert(fixture.calls[1] == CALL_SGX_ON);

    assert(hv_agx_power_broker_command(&broker, HV_AGX_POWER_CMD_ON, 2));
    assert(fixture.count == 2);

    assert(hv_agx_power_broker_command(&broker, HV_AGX_POWER_CMD_OFF, 3));
    assert(fixture.count == 4);
    assert(fixture.calls[2] == CALL_SGX_OFF);
    assert(fixture.calls[3] == CALL_ASC_OFF);

    assert(hv_agx_power_broker_command(&broker, HV_AGX_POWER_CMD_OFF, 4));
    assert(fixture.count == 4);
    hv_agx_power_broker_snapshot(&broker, &snapshot);
    assert(snapshot.state == HV_AGX_POWER_OFF);
    assert(snapshot.receipt_sequence == 4);
}

static void test_partial_on_rolls_back_and_fails_closed(void)
{
    struct fixture fixture = {.fail_call = CALL_SGX_ON};
    struct hv_agx_power_broker broker = make_broker(&fixture);
    struct hv_agx_power_snapshot snapshot;

    assert(!hv_agx_power_broker_command(&broker, HV_AGX_POWER_CMD_ON, 7));
    assert(fixture.count == 3);
    assert(fixture.calls[0] == CALL_ASC_ON);
    assert(fixture.calls[1] == CALL_SGX_ON);
    assert(fixture.calls[2] == CALL_ASC_OFF);
    hv_agx_power_broker_snapshot(&broker, &snapshot);
    assert(snapshot.state == HV_AGX_POWER_FAILED);
    assert(snapshot.receipt_sequence == 7);
    assert(snapshot.result == HV_AGX_POWER_RESULT_TRANSITION_FAILED);
}

static void test_rejects_invalid_and_non_monotonic_requests(void)
{
    struct fixture fixture = {0};
    struct hv_agx_power_broker broker = make_broker(&fixture);
    struct hv_agx_power_snapshot snapshot;

    assert(!hv_agx_power_broker_command(&broker, 0xffffffffu, 1));
    hv_agx_power_broker_snapshot(&broker, &snapshot);
    assert(snapshot.result == HV_AGX_POWER_RESULT_INVALID_COMMAND);
    assert(snapshot.receipt_sequence == 1);
    assert(!hv_agx_power_broker_command(&broker, HV_AGX_POWER_CMD_QUERY, 1));
    assert(!hv_agx_power_broker_command(&broker, HV_AGX_POWER_CMD_QUERY, 0));
    hv_agx_power_broker_snapshot(&broker, &snapshot);
    assert(snapshot.result == HV_AGX_POWER_RESULT_STALE_SEQUENCE);
    assert(snapshot.rejected_requests == 3);
    assert(fixture.count == 0);
}

static void test_mmio_abi_is_exact_and_command_is_last_write(void)
{
    struct fixture fixture = {0};
    struct hv_agx_power_broker broker = make_broker(&fixture);
    uint64_t value = 0;

    assert(hv_agx_power_broker_mmio(&broker, HV_AGX_POWER_REG_MAGIC, &value, false, 2));
    assert(value == HV_AGX_POWER_MAGIC);
    assert(!hv_agx_power_broker_mmio(&broker, HV_AGX_POWER_REG_MAGIC, &value, false, 3));
    assert(!hv_agx_power_broker_mmio(&broker, HV_AGX_POWER_REG_MAGIC, &value, true, 2));
    assert(!hv_agx_power_broker_mmio(&broker, 0x100, &value, false, 2));

    value = 1;
    assert(hv_agx_power_broker_mmio(&broker, HV_AGX_POWER_REG_REQUEST_SEQUENCE, &value,
                                    true, 3));
    assert(fixture.count == 0);
    value = HV_AGX_POWER_CMD_ON;
    assert(hv_agx_power_broker_mmio(&broker, HV_AGX_POWER_REG_COMMAND, &value, true, 2));
    assert(fixture.count == 2);

    value = 0;
    assert(hv_agx_power_broker_mmio(&broker, HV_AGX_POWER_REG_RECEIPT_SEQUENCE, &value,
                                    false, 3));
    assert(value == 1);
    assert(hv_agx_power_broker_mmio(&broker, HV_AGX_POWER_REG_STATE, &value, false, 2));
    assert(value == HV_AGX_POWER_ON);
    assert(!hv_agx_power_broker_mmio(&broker, HV_AGX_POWER_REG_COMMAND, &value, true, 3));
}

static void test_j313_platform_ops_have_no_arbitrary_path_surface(void)
{
    struct hv_agx_power_broker broker;
    const struct hv_agx_power_ops *ops = hv_agx_power_j313_ops();

    platform_count = 0;
    hv_agx_power_broker_init(&broker, ops, NULL);
    assert(hv_agx_power_broker_command(&broker, HV_AGX_POWER_CMD_ON, 1));
    assert(hv_agx_power_broker_command(&broker, HV_AGX_POWER_CMD_OFF, 2));
    assert(platform_count == 4);
    assert(platform_enables[0] && strcmp(platform_paths[0], "/arm-io/gfx-asc") == 0);
    assert(platform_enables[1] && strcmp(platform_paths[1], "/arm-io/sgx") == 0);
    assert(!platform_enables[2] && strcmp(platform_paths[2], "/arm-io/sgx") == 0);
    assert(!platform_enables[3] && strcmp(platform_paths[3], "/arm-io/gfx-asc") == 0);
}

int main(void)
{
    test_initial_snapshot_and_query_receipt();
    test_on_off_order_and_idempotence();
    test_partial_on_rolls_back_and_fails_closed();
    test_rejects_invalid_and_non_monotonic_requests();
    test_mmio_abi_is_exact_and_command_is_last_write();
    test_j313_platform_ops_have_no_arbitrary_path_surface();
    return 0;
}
