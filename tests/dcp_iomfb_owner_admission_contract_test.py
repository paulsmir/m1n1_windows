#!/usr/bin/env python3
"""Regression contract for fail-closed IOMFB owner admission ordering."""

from pathlib import Path


source = (Path(__file__).parents[1] / "src" / "dcp.c").read_text()
makefile = (Path(__file__).parents[1] / "Makefile").read_text()
start = source.index("bool dcp_iomfb_owner_start(")
end = source.index("\nbool dcp_iomfb_owner_supported", start)
body = source[start:end]

start_endpoint = body.index("rtkit_start_ep(")
observe_start = body.index("dcp_iomfb_observe_start_fail_closed(")
settle_system = body.index("dcp_iomfb_drain_pending_system_traffic(")
init_piodma = body.index("dart_init_adt(")
allocate_shmem = body.index("rtkit_alloc_buffer_aligned(")
register_handler = body.index("afk_epic_register_raw_handler(")
send_shmem = body.index("dcp_iomfb_set_shmem_message(")

assert start_endpoint < observe_start < init_piodma, (
    "START-only observation must run before any PIODMA state is touched"
)
observe_body = source[source.index(
    "static bool dcp_iomfb_observe_start_fail_closed"
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
assert "defined(DCP_IOMFB_START_OBSERVER)" in body
assert "defined(DCP_IOMFB_EARLY_PIODMA_OBSERVER)" in body
assert "defined(DCP_IOMFB_SET_SHMEM_OBSERVER)" in body
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

init_start = source.index("dcp_dev_t *dcp_init(")
init_end = source.index("\nint dcp_shutdown", init_start)
init_body = source[init_start:init_end]
early_init = init_body.index("DCP_IOMFB_EARLY_PIODMA_OBSERVER")
early_piodma = init_body.index(
    'dart_init_adt("/arm-io/dart-disp0", 0, 4, true)', early_init
)
rtkit_boot = init_body.index("rtkit_boot(")
assert early_init < early_piodma < rtkit_boot, (
    "the early PIODMA discriminator must configure SID4 before RTKit boot"
)
assert "IOMFB_EARLY_PIODMA_OBSERVER requires IOMFB_FULL_OWNER=1" in makefile
assert "IOMFB_START_OBSERVER and IOMFB_EARLY_PIODMA_OBSERVER are mutually exclusive" in makefile
assert "IOMFB_SET_SHMEM_OBSERVER requires IOMFB_FULL_OWNER=1" in makefile
assert "IOMFB_START_OBSERVER and IOMFB_SET_SHMEM_OBSERVER are mutually exclusive" in makefile
assert "IOMFB_EARLY_PIODMA_OBSERVER and IOMFB_SET_SHMEM_OBSERVER are mutually exclusive" in makefile

set_shmem_observer = source[source.index(
    "static bool dcp_iomfb_observe_set_shmem_fail_closed"
):source.index("bool dcp_iomfb_owner_start(")]
assert "rtkit_alloc_buffer_aligned" in set_shmem_observer
assert "DCP_IOMFB_RPC_SHMEM_SIZE, 0x10000" in set_shmem_observer
assert "dcp_iomfb_set_shmem_message" in set_shmem_observer
assert "DCP_IOMFB_MESSAGE_TYPE_INITIALIZED" in set_shmem_observer
assert "dcp_iomfb_bootstrap_start" not in set_shmem_observer
assert "afk_epic_register_raw_handler" not in set_shmem_observer
assert "return false;" in set_shmem_observer

set_shmem_call = body.index("dcp_iomfb_observe_set_shmem_fail_closed(")
assert start_endpoint < set_shmem_call < init_piodma, (
    "SET_SHMEM observer must run immediately after START with early SID4"
)
assert "defined(DCP_IOMFB_SET_SHMEM_OBSERVER)" in init_body

print("dcp_iomfb_owner_admission_contract_test: ok")
