#!/usr/bin/env python3

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
chainload = (ROOT / "proxyclient/tools/chainload.py").read_text()
dcp = (ROOT / "src/dcp.c").read_text()

assert "m1n1_iomfb_utc_ms" in chainload
assert "m1n1_iomfb_utc_cntpct" in chainload
assert "m1n1_iomfb_utc_cntfrq" in chainload
assert 'u.mrs("CNTPCT_EL0")' in chainload
assert 'u.mrs("CNTFRQ_EL0")' in chainload
assert '"m1n1-iomfb-utc-cntpct"' in dcp
assert '"m1n1-iomfb-utc-cntfrq"' in dcp
assert "dcp_iomfb_clock_now" in dcp
assert "D209 has no authoritative UTC counter anchor" in dcp

print("dcp_iomfb_clock_anchor_contract_test: PASS")
