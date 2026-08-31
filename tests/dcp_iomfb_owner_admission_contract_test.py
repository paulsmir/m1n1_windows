#!/usr/bin/env python3
"""Regression contract for fail-closed IOMFB owner admission ordering."""

from pathlib import Path


source = (Path(__file__).parents[1] / "src" / "dcp.c").read_text()
start = source.index("bool dcp_iomfb_owner_start(")
end = source.index("\nbool dcp_iomfb_owner_supported", start)
body = source[start:end]

start_endpoint = body.index("rtkit_start_ep(")
observe_start = body.index("dcp_iomfb_observe_start_without_piodma(")
settle_system = body.index("dcp_iomfb_drain_pending_system_traffic(")
init_piodma = body.index("dart_init_adt(")
allocate_shmem = body.index("rtkit_alloc_buffer_aligned(")
register_handler = body.index("afk_epic_register_raw_handler(")
send_shmem = body.index("dcp_iomfb_set_shmem_message(")

assert start_endpoint < observe_start < init_piodma, (
    "START-only observation must run before any PIODMA state is touched"
)
observe_body = source[source.index(
    "static bool dcp_iomfb_observe_start_without_piodma"
):start]
assert "DCP_IOMFB_START_OBSERVE_MAX_POLLS" in source
assert "DCP_IOMFB_START_OBSERVE_USEC" in source
assert "timeout_calculate(DCP_IOMFB_START_OBSERVE_USEC)" in observe_body
assert "timeout_expired(deadline)" in observe_body
assert "rtkit_recv_one_quiet(" in observe_body
assert "msg.ep == DCP_IOMFB_RPC_ENDPOINT" in observe_body
assert "return false;" in observe_body, (
    "the receipt-only experiment must fail closed for every observation"
)
assert "#ifdef DCP_IOMFB_START_OBSERVER" in body
assert "#ifndef DCP_IOMFB_START_OBSERVER" in body
assert settle_system < start_endpoint, (
    "ordinary full-owner builds must retain the accepted pre-START drain"
)
assert start_endpoint < init_piodma, (
    "IOMFB endpoint must start before creating its PIODMA mapping"
)
assert start_endpoint < allocate_shmem < send_shmem, (
    "IOMFB shared memory must be allocated only after endpoint start and before SET_SHMEM"
)
assert start_endpoint < register_handler < send_shmem, (
    "the raw handler must be ready before SET_SHMEM, but only after endpoint start"
)

after_start = body[start_endpoint:]
assert "fail_endpoint:" in after_start
fail_endpoint = after_start.index("fail_endpoint:")
before_next_label = after_start.find("\nfail_", fail_endpoint + len("fail_endpoint:"))
fail_body = after_start[fail_endpoint:before_next_label]
assert "rtkit_free_buffer" not in fail_body
assert "dart_shutdown" not in fail_body
assert "return false;" in fail_body, (
    "post-START failure must retain endpoint-owned resources until reset"
)

print("dcp_iomfb_owner_admission_contract_test: ok")
