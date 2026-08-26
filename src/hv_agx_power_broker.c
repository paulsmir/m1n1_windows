/* SPDX-License-Identifier: MIT */

#include "hv_agx_power_broker.h"

static bool transition_active(enum hv_agx_power_state state)
{
    return state == HV_AGX_POWER_ENABLING_ASC || state == HV_AGX_POWER_ENABLING_SGX ||
           state == HV_AGX_POWER_DISABLING_SGX || state == HV_AGX_POWER_DISABLING_ASC;
}

void hv_agx_power_broker_init(struct hv_agx_power_broker *broker,
                              const struct hv_agx_power_ops *ops, void *opaque)
{
    if (!broker || !ops)
        return;

    *broker = (struct hv_agx_power_broker){
        .ops = *ops,
        .opaque = opaque,
        .state = HV_AGX_POWER_OFF,
        .result = HV_AGX_POWER_RESULT_OK,
    };
}

static bool finish(struct hv_agx_power_broker *broker, uint64_t sequence,
                   enum hv_agx_power_result result, bool accepted)
{
    broker->receipt_sequence = sequence;
    broker->result = result;
    if (accepted)
        broker->accepted_requests++;
    else
        broker->rejected_requests++;
    return accepted;
}

static bool power_on(struct hv_agx_power_broker *broker, uint64_t sequence)
{
    if (broker->state == HV_AGX_POWER_ON)
        return finish(broker, sequence, HV_AGX_POWER_RESULT_OK, true);
    if (broker->state != HV_AGX_POWER_OFF || !broker->ops.asc_on || !broker->ops.sgx_on ||
        !broker->ops.asc_off)
        return finish(broker, sequence, HV_AGX_POWER_RESULT_TRANSITION_FAILED, false);

    broker->state = HV_AGX_POWER_ENABLING_ASC;
    if (!broker->ops.asc_on(broker->opaque)) {
        broker->state = HV_AGX_POWER_FAILED;
        return finish(broker, sequence, HV_AGX_POWER_RESULT_TRANSITION_FAILED, false);
    }

    broker->state = HV_AGX_POWER_ENABLING_SGX;
    if (!broker->ops.sgx_on(broker->opaque)) {
        broker->ops.asc_off(broker->opaque);
        broker->state = HV_AGX_POWER_FAILED;
        return finish(broker, sequence, HV_AGX_POWER_RESULT_TRANSITION_FAILED, false);
    }

    broker->state = HV_AGX_POWER_ON;
    return finish(broker, sequence, HV_AGX_POWER_RESULT_OK, true);
}

static bool power_off(struct hv_agx_power_broker *broker, uint64_t sequence)
{
    if (broker->state == HV_AGX_POWER_OFF)
        return finish(broker, sequence, HV_AGX_POWER_RESULT_OK, true);
    if (broker->state != HV_AGX_POWER_ON || !broker->ops.sgx_off || !broker->ops.asc_off)
        return finish(broker, sequence, HV_AGX_POWER_RESULT_TRANSITION_FAILED, false);

    broker->state = HV_AGX_POWER_DISABLING_SGX;
    if (!broker->ops.sgx_off(broker->opaque)) {
        broker->state = HV_AGX_POWER_FAILED;
        return finish(broker, sequence, HV_AGX_POWER_RESULT_TRANSITION_FAILED, false);
    }

    broker->state = HV_AGX_POWER_DISABLING_ASC;
    if (!broker->ops.asc_off(broker->opaque)) {
        broker->state = HV_AGX_POWER_FAILED;
        return finish(broker, sequence, HV_AGX_POWER_RESULT_TRANSITION_FAILED, false);
    }

    broker->state = HV_AGX_POWER_OFF;
    return finish(broker, sequence, HV_AGX_POWER_RESULT_OK, true);
}

bool hv_agx_power_broker_command(struct hv_agx_power_broker *broker, uint32_t command,
                                 uint64_t request_sequence)
{
    if (!broker)
        return false;
    if (!request_sequence || request_sequence <= broker->receipt_sequence) {
        broker->result = HV_AGX_POWER_RESULT_STALE_SEQUENCE;
        broker->rejected_requests++;
        return false;
    }
    if (transition_active(broker->state))
        return finish(broker, request_sequence, HV_AGX_POWER_RESULT_BUSY, false);

    switch (command) {
        case HV_AGX_POWER_CMD_QUERY:
            return finish(broker, request_sequence, HV_AGX_POWER_RESULT_OK, true);
        case HV_AGX_POWER_CMD_ON:
            return power_on(broker, request_sequence);
        case HV_AGX_POWER_CMD_OFF:
            return power_off(broker, request_sequence);
        default:
            return finish(broker, request_sequence, HV_AGX_POWER_RESULT_INVALID_COMMAND,
                          false);
    }
}

void hv_agx_power_broker_snapshot(const struct hv_agx_power_broker *broker,
                                  struct hv_agx_power_snapshot *snapshot)
{
    if (!broker || !snapshot)
        return;

    *snapshot = (struct hv_agx_power_snapshot){
        .magic = HV_AGX_POWER_MAGIC,
        .abi_version = HV_AGX_POWER_ABI_VERSION,
        .capabilities = HV_AGX_POWER_CAP_FIXED_J313_DOMAINS,
        .state = broker->state,
        .result = broker->result,
        .receipt_sequence = broker->receipt_sequence,
        .accepted_requests = broker->accepted_requests,
        .rejected_requests = broker->rejected_requests,
    };
}

bool hv_agx_power_broker_mmio(struct hv_agx_power_broker *broker, uint64_t offset,
                              uint64_t *value, bool write, unsigned width)
{
    struct hv_agx_power_snapshot snapshot;

    if (!broker || !value)
        return false;

    if (write) {
        if (offset == HV_AGX_POWER_REG_REQUEST_SEQUENCE && width == 3) {
            broker->pending_sequence = *value;
            return true;
        }
        if (offset == HV_AGX_POWER_REG_COMMAND && width == 2) {
            hv_agx_power_broker_command(broker, (uint32_t)*value, broker->pending_sequence);
            return true;
        }
        return false;
    }

    hv_agx_power_broker_snapshot(broker, &snapshot);
    if (width == 2) {
        switch (offset) {
            case HV_AGX_POWER_REG_MAGIC:
                *value = snapshot.magic;
                return true;
            case HV_AGX_POWER_REG_ABI_VERSION:
                *value = snapshot.abi_version;
                return true;
            case HV_AGX_POWER_REG_CAPABILITIES:
                *value = snapshot.capabilities;
                return true;
            case HV_AGX_POWER_REG_STATE:
                *value = snapshot.state;
                return true;
            case HV_AGX_POWER_REG_RESULT:
                *value = snapshot.result;
                return true;
            default:
                return false;
        }
    }
    if (width == 3) {
        switch (offset) {
            case HV_AGX_POWER_REG_RECEIPT_SEQUENCE:
                *value = snapshot.receipt_sequence;
                return true;
            case HV_AGX_POWER_REG_ACCEPTED_REQUESTS:
                *value = snapshot.accepted_requests;
                return true;
            case HV_AGX_POWER_REG_REJECTED_REQUESTS:
                *value = snapshot.rejected_requests;
                return true;
            case HV_AGX_POWER_REG_REQUEST_SEQUENCE:
                *value = broker->pending_sequence;
                return true;
            default:
                return false;
        }
    }
    return false;
}
