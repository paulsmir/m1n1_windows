#!/usr/bin/env python3
"""Regression contract for RTKit application-endpoint power admission."""

from pathlib import Path


source = (Path(__file__).parents[1] / "src" / "rtkit.c").read_text()
start = source.index("bool rtkit_boot(")
end = source.index("\nstatic bool rtkit_switch_power_state", start)
body = source[start:end]

send_ap_on = body.index("MGMT_MSG_AP_PWR_STATE")
return_success = body.rindex("return true;")
after_send = body[send_ap_on:return_success]

assert "rtk->ap_power != RTKIT_POWER_ON" in after_send, (
    "rtkit_boot must not return before the AP power state reaches ON"
)
assert "RTKIT_POWER_ACK_TIMEOUT_USEC" in after_send, (
    "the AP power acknowledgement wait must be bounded"
)
assert "rtkit_recv(rtk" in after_send, (
    "the AP power acknowledgement wait must pump RTKit management messages"
)

print("rtkit_power_ack_contract_test: ok")
