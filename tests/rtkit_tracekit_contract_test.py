#!/usr/bin/env python3
"""Regression contract for the TraceKit system endpoint advertised by DCP."""

from pathlib import Path


source = (Path(__file__).parents[1] / "src" / "rtkit.c").read_text()

assert "#define RTKIT_EP_TRACEKIT 0xa" in source, (
    "RTKit endpoint 0x0a must be identified as TraceKit"
)
boot_start = source.index("bool rtkit_boot(")
boot_end = source.index("\nstatic bool rtkit_switch_power_state", boot_start)
boot = source[boot_start:boot_end]
assert "has_tracekit" in boot, "EPMAP parsing must retain TraceKit advertisement"
assert "rtkit_start_ep(rtk, RTKIT_EP_TRACEKIT)" in boot, (
    "advertised TraceKit must be started with the required system endpoints"
)
start_order = [
    boot.index(f"rtkit_start_ep(rtk, {endpoint})")
    for endpoint in (
        "RTKIT_EP_CRASHLOG",
        "RTKIT_EP_SYSLOG",
        "RTKIT_EP_DEBUG",
        "RTKIT_EP_IOREPORT",
        "RTKIT_EP_OSLOG",
        "RTKIT_EP_TRACEKIT",
    )
]
assert start_order == sorted(start_order), (
    "system endpoints must follow upstream's ascending EPMAP order"
)

print("rtkit_tracekit_contract_test: ok")
