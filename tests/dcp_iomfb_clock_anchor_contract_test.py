#!/usr/bin/env python3

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
chainload = (ROOT / "proxyclient/tools/chainload.py").read_text()
dcp = (ROOT / "src/dcp.c").read_text()

assert 'set_chosen_u64("m1n1-iomfb-utc-ms"' in chainload
assert 'set_chosen_u64("m1n1-iomfb-utc-cntpct"' in chainload
assert 'set_chosen_u64("m1n1-iomfb-utc-cntfrq"' in chainload
assert 'u.mrs("CNTPCT_EL0")' in chainload
assert 'u.mrs("CNTFRQ_EL0")' in chainload
assert "from construct import Int64ul" in chainload
assert 'chosen._types[name] = (Int64ul, False)' in chainload
assert '"m1n1-iomfb-utc-cntpct"' in dcp
assert '"m1n1-iomfb-utc-cntfrq"' in dcp
assert "dcp_iomfb_clock_now" in dcp
assert "D209 has no authoritative UTC counter anchor" in dcp

print("dcp_iomfb_clock_anchor_contract_test: PASS")
