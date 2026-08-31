#!/usr/bin/env python3
"""Regression contract for serializing AFK TX-ring publication."""

from pathlib import Path


source = (Path(__file__).parents[1] / "src" / "afk.c").read_text()
start = source.index("static int afk_epic_tx_mode(")
end = source.index("\nstatic int afk_epic_tx(", start)
body = source[start:end]

reserve = body.index("rtkit_try_reserve_send")
read_rptr = body.index("rb->hdr->rptr")
read_wptr = body.index("rb->hdr->wptr")
publish_wptr = body.index("rb->hdr->wptr = wptr")
commit = body.index("rtkit_commit_reserved_send")

assert reserve < read_rptr < read_wptr < publish_wptr < commit, (
    "the ASC reservation must cover AFK ring selection, publication, and doorbell"
)

buffer_full = body.index("buffer_full:")
assert "rtkit_cancel_reserved_send" in body[buffer_full:], (
    "a full AFK ring must release the reserved ASC mailbox owner"
)

print("afk_tx_reservation_contract_test: ok")
