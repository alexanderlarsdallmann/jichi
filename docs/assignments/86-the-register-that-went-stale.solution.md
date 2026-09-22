# 86 — the register that went stale: a worked example

*The reference `AUDIT.md`, with the check that produced each verdict. Read this
**after** you have written your own — the point of the task is the checking,
and a worked example read first replaces it.*

---

<!-- AUDIT.md -->
R1: STALE — project/upload.c

R2: STANDS

R3: STALE — project/FORMAT.md

R4: STANDS

R5: STANDS
<!-- /AUDIT.md -->

---

## What produced each verdict

The register's own preamble says *"last walked: eleven months ago"*, which is
the only warning you get. Nothing else on the page announces that a row has
expired — a stale row looks exactly like a live one, because it was true when
it was written.

**R1 — no retry on a failed upload. STALE.** `project/upload.c` defines
`MAX_ATTEMPTS 3` and loops over it, logging `attempt %d failed, retrying`. The
work the row defers is done. *Check:* open the file the row's own `Where:`
column names. This is the cheapest possible audit and it resolves the row in
one read.

**R2 — no timeout on the network read. STANDS.** Nothing in `project/` sets a
timeout: no `SO_RCVTIMEO`, no `alarm`, no deadline of any kind. `send_once` is
declared and never defined here, so the socket handling is not even in this
tree to inspect. *Check:* grep for the mechanism, then confirm by reading —
absence is the one verdict a grep can nearly establish, because you are looking
for nothing rather than for something that might be spelled differently.

**R3 — the wire format is undocumented. STALE.** `project/FORMAT.md` exists and
documents it: field order, separator, encoding, and the forward-compatibility
rule. *Check:* the row's `Where:` says `project/`, so list it. The row is stale
not because somebody wrote the document badly but because they wrote it and did
not come back here.

**R4 — no checksum on a stored record. STANDS.** `project/store.c` is nine
lines: open, `fprintf`, close. No checksum, no CRC, nothing that could serve as
one. *Check:* read the whole file. It is short enough that reading beats
grepping, and reading is the only way to be sure a checksum is not present
under another name.

**R5 — no rate limiting on the client. STANDS, and this is the one to get
wrong.** `grep -i rate project/upload.c` returns four hits:

```
/* The sampler hands us records at a fixed sample rate; this is the rate the
static int sample_rate_hz = 50;
int sampler_rate(void)
    return sample_rate_hz;
```

Every one of them is a **sampler's** rate — how often the device produces a
record — and the comment says outright that it is *"not anything we control or
limit here"*. There is no limiter, no budget, no backpressure. The row stands.

A grep said *yes*; the file said *no*. If you called R5 stale, you did not make
a small error — you retired work that still needs doing, on evidence that
matched a word rather than a thing.

## Why both directions are graded

The grader fails a missed stale row and an invented one alike, and that is
deliberate. It is tempting to treat a false positive as the safer mistake — you
looked, you were keen. It is not safer. A missed row leaves a stale claim on a
page somebody will eventually re-check. A false one **deletes a real piece of
work from the register**, and nobody re-checks a row that is no longer there.

The habit this task is for is one sentence: **check the checkable part of a
reason before you act on it, and check it by opening the thing rather than by
matching its name.** Registers in this project carry that rule at the top of
[`DEFERRED.md`](../DEFERRED.md) precisely because it has been broken here, more
than once, by exactly the R5 move.
