#!/usr/bin/env python3
"""Regression contract for fail-closed IOMFB owner admission ordering."""

from pathlib import Path


source = (Path(__file__).parents[1] / "src" / "dcp.c").read_text()
start = source.index("bool dcp_iomfb_owner_start(")
end = source.index("\nbool dcp_iomfb_owner_supported", start)
body = source[start:end]

start_endpoint = body.index("rtkit_start_ep(")
init_piodma = body.index("dart_init_adt(")
allocate_shmem = body.index("rtkit_alloc_buffer_aligned(")
register_handler = body.index("afk_epic_register_raw_handler(")
send_shmem = body.index("dcp_iomfb_set_shmem_message(")

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
