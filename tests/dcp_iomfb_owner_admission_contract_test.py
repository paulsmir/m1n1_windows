#!/usr/bin/env python3
"""Regression contract for fail-closed IOMFB owner admission ordering."""

from pathlib import Path


source = (Path(__file__).parents[1] / "src" / "dcp.c").read_text()
main_source = (Path(__file__).parents[1] / "src" / "main.c").read_text()
display_header = (Path(__file__).parents[1] / "src" / "display.h").read_text()
makefile = (Path(__file__).parents[1] / "Makefile").read_text()
start = source.index("bool dcp_iomfb_owner_start(")
end = source.index("\nbool dcp_iomfb_owner_supported", start)
body = source[start:end]

start_endpoint = body.index("rtkit_start_ep(")
observe_start = body.index("dcp_iomfb_observe_start_fail_closed(")
allocate_shmem = body.index("rtkit_alloc_buffer_aligned(")
register_handler = body.index("afk_epic_register_raw_handler(")
send_shmem = body.index("dcp_iomfb_set_shmem_message(")

assert start_endpoint < observe_start < allocate_shmem, (
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
assert "DCP_IOMFB_SET_SHMEM_OBSERVER" in body
assert "dcp_iomfb_drain_pending_system_traffic(" not in body, (
    "production admission must not depend on opportunistic pre-START traffic"
)
assert "discriminator not active" not in source, (
    "an empty RTKit queue is a valid protocol state, not a production failure"
)
assert 'dart_init_adt("/arm-io/dart-disp0", 0, 4, true)' not in body, (
    "full-owner PIODMA must already exist before application endpoint START"
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
early_init = init_body.index("DCP_IOMFB_FULL_OWNER")
early_piodma = init_body.index(
    'dart_init_adt("/arm-io/dart-disp0", 0, 4, true)', early_init
)
rtkit_boot = init_body.index("rtkit_boot(")
assert early_init < early_piodma < rtkit_boot, (
    "every full-owner build must configure SID4 before RTKit boot"
)
assert "IOMFB_EARLY_PIODMA_OBSERVER requires IOMFB_FULL_OWNER=1" in makefile
assert "IOMFB_START_OBSERVER and IOMFB_EARLY_PIODMA_OBSERVER are mutually exclusive" in makefile
assert "IOMFB_SET_SHMEM_OBSERVER requires IOMFB_FULL_OWNER=1" in makefile
assert "IOMFB_A401_OBSERVER requires IOMFB_FULL_OWNER=1" in makefile
assert "IOMFB_A426_OBSERVER requires IOMFB_FULL_OWNER=1" in makefile
assert "IOMFB_A449_OBSERVER requires IOMFB_FULL_OWNER=1" in makefile
assert "IOMFB_A456_OBSERVER requires IOMFB_FULL_OWNER=1" in makefile
assert "IOMFB_A411_OBSERVER requires IOMFB_FULL_OWNER=1" in makefile
assert "IOMFB_A472_OBSERVER requires IOMFB_FULL_OWNER=1" in makefile
assert "IOMFB_A410_OBSERVER requires IOMFB_FULL_OWNER=1" in makefile
assert "IOMFB_START_OBSERVER and IOMFB_SET_SHMEM_OBSERVER are mutually exclusive" in makefile
assert "IOMFB_EARLY_PIODMA_OBSERVER and IOMFB_SET_SHMEM_OBSERVER are mutually exclusive" in makefile
assert "IOMFB_A401_OBSERVER is mutually exclusive with earlier IOMFB observers" in makefile
assert "IOMFB_A426_OBSERVER is mutually exclusive with earlier IOMFB observers" in makefile
assert "IOMFB_A449_OBSERVER is mutually exclusive with earlier IOMFB observers" in makefile
assert "IOMFB_A456_OBSERVER is mutually exclusive with earlier IOMFB observers" in makefile
assert "IOMFB_A411_OBSERVER is mutually exclusive with earlier IOMFB observers" in makefile
assert "IOMFB_A472_OBSERVER is mutually exclusive with earlier IOMFB observers" in makefile
assert "IOMFB_A410_OBSERVER is mutually exclusive with earlier IOMFB observers" in makefile

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
assert start_endpoint < set_shmem_call < allocate_shmem, (
    "SET_SHMEM observer must run immediately after START with early SID4"
)
assert "#ifdef DCP_IOMFB_FULL_OWNER" in init_body

a401 = body.index('dcp_iomfb_owner_call(dcp, "A401"')
bootstrap = body.index("dcp_iomfb_bootstrap_start(")
assert send_shmem < a401 < bootstrap, (
    "A401-only observer must reuse proven SET_SHMEM and stop before downstream bootstrap"
)
assert "A401 admitted result=%u; downstream calls disabled" in body

a426 = body.index("dcp_iomfb_bootstrap_start_through_color_remap(")
assert send_shmem < a426 < bootstrap, (
    "A426-only observer must reuse proven SET_SHMEM and stop before the remaining bootstrap"
)
assert "A426 admitted; downstream calls disabled" in body

a449 = body.index("dcp_iomfb_bootstrap_start_through_video_power_savings(")
assert a426 < a449 < bootstrap, (
    "A449-only observer must extend the accepted A426 step and stop before A456/A411"
)
assert "A449 admitted; downstream calls disabled" in body

a456 = body.index("dcp_iomfb_bootstrap_start_through_first_client_open(")
assert a449 < a456 < bootstrap, (
    "A456-only observer must extend the accepted A449 step and stop before A411"
)
assert "A456 admitted; downstream calls disabled" in body

a411 = body.index("A411 admitted main_display=%u; downstream calls disabled")
assert a456 < bootstrap < a411, (
    "A411-only observer must use the full accepted bootstrap before production continues"
)
assert body.rfind("dcp_iomfb_bootstrap_start(", 0, a411) > a456

a472 = body.index("A472 admitted; downstream calls disabled")
assert a411 < a472, "A472-only observer must extend the accepted A411 boundary"
assert body.rfind("dcp_iomfb_bootstrap_power_on_firmware(", 0, a472) > a411

a410 = body.index("A410 admitted; downstream calls disabled")
assert a472 < a410, "A410-only observer must extend the accepted A472 boundary"
assert body.rfind("dcp_iomfb_bootstrap_select_display(", 0, a410) > a472

color_property = body.index(
    'dcp_iomfb_properties_find(&dcp->iomfb_properties, "ColorElements"'
)
timing_property = body.index(
    'dcp_iomfb_properties_find(&dcp->iomfb_properties, "TimingElements"'
)
select_modes = body.index("dcp_iomfb_select_modes(")
power_on = body.index("dcp_iomfb_bootstrap_power_on(")
modeset = body.index("dcp_iomfb_bootstrap_modeset(")
assert bootstrap < color_property < timing_property < select_modes < power_on < modeset, (
    "production owner must consume validated DCPAV modes, power the panel and "
    "perform A412 before it admits presentation"
)

active_start = source.index("bool dcp_iomfb_owner_active(")
active_end = source.index("\nvoid dcp_iomfb_owner_arm", active_start)
active_body = source[active_start:active_end]
assert "DCP_IOMFB_BOOT_MODESET" in active_body
assert "DCP_IOMFB_BOOT_ACTIVE" not in active_body

poll_start = source.index("int dcp_iomfb_owner_poll_latch(")
poll_end = source.index("\nint dcp_iomfb_owner_present", poll_start)
poll_body = source[poll_start:poll_end]
assert "DCP_IOMFB_BOOT_MODESET" in poll_body
assert "DCP_IOMFB_BOOT_ACTIVE" not in poll_body

terminal_start = main_source.index("#if defined(DCP_IOMFB_A401_OBSERVER)")
terminal_end = main_source.index("#endif", terminal_start)
terminal = main_source[terminal_start:terminal_end]
assert terminal.index("display_shutdown_complete()") < terminal.index("uartproxy_run(NULL)")
assert terminal.index("uartproxy_run(NULL)") < terminal.index(
    'panic("IOMFB admission observer proxy returned'
)
assert "payload execution disabled" in terminal
assert "run_actions(" not in terminal and "next_stage" not in terminal
assert "bool display_shutdown_complete(void);" in display_header

print("dcp_iomfb_owner_admission_contract_test: ok")
