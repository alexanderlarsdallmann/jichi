# A downstream workload, measured: 36,925 events

*A private downstream workspace's `full`-tier telemetry, supplied by the operator
on 2026-08-07 and analysed offline. The workspace is not named here or anywhere
committed; only aggregate figures leave this analysis.*

| | |
|---|---|
| window | 2026-07-27 → 2026-08-07 (11 days) |
| events | 36,925 |
| sessions | 35 |
| turns | 369 |
| model calls | 17,365 |
| tool calls | 14,988 |
| input tokens | 1,238,237,582 |
| output tokens | 2,977,965 |

---

## 1. The connect timeout: an episode, not a rate

**2,437 of 17,365 model calls (14.0%) failed** — every one with `status: 0`, no
HTTP response, retried as `"transient"`. Their latency clusters at **10,003 ms**,
with p10 through p90 spanning two milliseconds. That is
`JC_HTTP_CONNECT_TIMEOUT_DEFAULT`, which was `10L`.

**The first correction.** Size is not the discriminator. Failed requests measure
~84k tokens against ~87k for successful ones (via nearest-preceding-success, since
the failures themselves record 0 — see §2). Nor is position in the turn, nor
session age.

**The second correction, and the important one.** The 14% is not an ongoing rate.
It is an episode:

| day | calls | failed | rate |
|---|---|---|---|
| 07-27 | 387 | 133 | 34.4% |
| 07-28 | 1,347 | 339 | 25.2% |
| 07-29 | 3,688 | 907 | 24.6% |
| 07-30 | 345 | 52 | 15.1% |
| 07-31 | 3,018 | 770 | 25.5% |
| 08-01 | 674 | 216 | 32.0% |
| 08-03 | 1,549 | 1 | 0.1% |
| 08-04 | 1,094 | 2 | 0.2% |
| 08-05 | 1,774 | 14 | 0.8% |
| 08-06 | 2,456 | 3 | 0.1% |
| 08-07 | 1,033 | 0 | 0.0% |

Something changed around 2026-08-02 — endpoint, network, or the operator's own
`timeouts.connect`. **7,906 calls since then have produced 20 failures.** Any claim
that jichi "loses 14% of calls" would be false today, and the first draft of this
analysis made it before the per-day split was run. Counts are not rates.

### What was nevertheless wrong in jichi

`http_handle_release(curl, rc == CURLE_OK)` dropped the pooled handle on **any**
error. A *connect* failure means no connection was ever established, so nothing
can have been poisoned — and dropping the handle discards the DNS and TLS-session
caches, forcing the retry to open a cold connection. That is the operation that
had just timed out. Self-reinforcing, and it is why the connect phase was on the
hot path at all: with a live handle most calls never enter it.

Fixed by `jc_http_conn_reusable(transfer_ok, connected)` (pure, unit-tested), and
the default raised `10L → 30L` to match the stall default. The trade is stated:
a genuinely unreachable endpoint now takes 30 s to report instead of 10 s, and
M321's message already names the knob that caused it.

### This was seen twice before and not fixed

- **M219** built `JC_FAULT_NET` because "the analyzed unattended workload logged
  ~2,400 transient transport failures" — the same phenomenon, used to exercise the
  retry ladder rather than to ask why the ladder was busy.
- **M321** diagnosed it precisely ("the case that cost an operator 6.5 hours") and
  improved the *error message*.

Twice observed, twice instrumented, cause untouched. Worth remembering as a
pattern: an instrument built around a symptom can make the symptom comfortable.

---

## 2. A failed call recorded that it had sent nothing

`in_tok` is read out of the response body. A call that never got a response logged
`in_tok: 0`, so **2,437 failed attempts claimed to have sent zero bytes**.

Two consequences, one of which bit this analysis directly:

- The retry cost was invisible. At the median successful request size, those
  attempts re-sent on the order of **211 MB of request bodies**, recorded as zero.
- The correlation in §1 could not be tested with the obvious field, because the
  failure zeroed the very number the question was about. Hence the
  nearest-preceding-success proxy.

`req_bytes` is now recorded on failure — the exact size, known at the moment of
sending, which survives the absence of a reply.

---

## 3. Repeat-failure loops, on jichi's own data at last

`DEFERRED.md`'s in-turn loop detector was **blocked on jichi's own data**
("telemetry was off and there were zero jichi-format sessions"). It no longer is:

| | Continue (M326r calibration) | **jichi (this workload)** |
|---|---|---|
| tool calls | 1,081 | 14,988 |
| errored | 177 (16.4%) | 1,269 (8.5%) |
| turns with ≥1 error | 50 | 203 |
| **repeating an identical failing call** | 25 (50%) | **75 (37%)** |

Tail: 2×:64, 3×:37, 4×:6, 5×:9 … 14×:2, 15×:1, **33×:1**. The worst is 33 attempts
at the same failing `edit_file` on one file inside one turn; then 15× and 14× the
same failing shell command. By tool: `run_terminal_command` 71, `edit_file` 23,
`run_tests` 8.

jichi's rate is better than the calibration agent's and the failure mode is
unambiguously present. The deferral's blocker is discharged; the design is not
part of this milestone.

---

## 4. Findings recorded, not acted on

**Mid-turn compaction thrash** — *followed up in M326x, and the metric was wrong.*
1,057 of 1,090 compactions are `midturn`. But **44% of those were the eager
zero-loss dedup, not pressure**: the honest split is 593 pressured / 464
housekeeping, and the raw count overstated the alarm by nearly half. Worse, the
`short` flag — "could not reach its target, the request went out over the limit" —
was emitted without consulting whether the pass was pressured at all, so **all 19
`short` events in this log were false and none were real**. Fixed in M326x; the
constants are deliberately untouched until the instrument can be trusted. The
thrash that remains is real: one turn ran **97 pressured passes** while its input
climbed 96k → 196k.

*Tuning attempted in M326y, and the constants were left alone on evidence.* A
pressured pass changes input by a median of **−19 tokens** (n=593; 49% of pressured
rounds grow anyway), because reclaim decays within a turn — 1st pass 10,324 tokens,
2nd 1,306, 3rd onward ~0 — as elided content falls under the 800-byte floor and is
never re-elided while newer results stay keep-recent protected. **174 of 593
pressured passes were the 26th or later in their turn.** Material is not the
constraint: 86% of tool-output bytes already exceed that floor. So no threshold
helps; the lever is smaller tool output. A new `unrelieved` flag names the turns
eliding cannot save.

**No prompt caching at all** — *followed up in M326w, and the assumption was worth
checking.* `cache_read_in` is 0 on every one of 17,365 calls, against 1.238 billion
input tokens and a **416:1** input:output ratio. Confirmed two ways: latency climbs
monotonically with input size (1.4 s → 9.2 s across bands), which a silent prefix
cache would flatten; and jichi's own prefix is byte-stable in **27 of 29 top-level
sessions**, so the 0% is a fact about the server rather than a varying prefix on our
side. The **fixed prefix is 12,637 tokens** (system 8,014 + tools 4,623), re-sent on
every call: **168M tokens, 14% of the total.** `doctor` now reports this; it was
diagnosable by `telemetry --cache-audit` since M104 but nothing said to run it.

**Hallucinated tool names — and M324 measurably worked.** Splitting at 2026-08-06,
when `list_files` gained `pattern` and `glob` became an alias:

| | before | on/after |
|---|---|---|
| `glob` | 46 calls, **46 failed** | **0 calls** |
| never-succeeding names | `todoedit` 14, `run_shell_command` 6, `create_file`, `todo_create`, … | `read_many` 1 |

The model stopped reaching for `glob` once the capability existed under the real
name. Live remnants: **5 tool calls with an empty name**, and `todowrite`
accounting for **303 of 335 `args_repair` events** (kind `unstring`), with a 75%
ok-rate against `todo_write`'s 97%.

**Tool failures overall** 8.5%, dominated by `run_terminal_command` (777 of 8,530).

---

## 5. A state directory inside the state directory

Not from the telemetry — from the drop's own shape. `~/.jichi.d/` contained
`~/.jichi.d/.jichi.d/`, holding **5.5 GB** of checkpoints across three workspaces,
last written 2026-07-27 08:37, immediately before the outer store took over.

The chain is ours:

1. `warn_legacy_paths()` fired only while `old exists && new does NOT`.
2. Running jichi once after upgrading **creates** the new directory — so from the
   first run the warning is permanently silent and nobody is told the old state is
   stranded.
3. Noticing later, you reach for `mv OLD NEW`. `NEW` now exists as a directory, so
   `mv` does not rename — it moves `OLD` *inside*, exit status 0, no output.
4. `MIGRATION.md` printed exactly that command four times, and jichi printed it
   too.

The both-exist case — the one that loses data — was the one with no guidance. It
now warns, refuses to print the `mv`, names the consequence, and the resulting
shape is detected by name. `MIGRATION.md` leads with the trap and gives a
check-first form; the per-cache-key moves at the end carry the same warning, where
it is *more* likely to bite because `<newkey>` exists the moment you run jichi from
the new path.

---

## What this changed

| finding | outcome |
|---|---|
| §1 connect timeout + handle drop | fixed (M326v) |
| §2 invisible retry size | fixed (M326v) |
| §5 migration nesting | fixed (M326v) |
| hooks invisible to telemetry | fixed (M326v) — a hook timeout now emits an event |
| §3 repeat loops | deferral unblocked; design pending |
| §4 caching | measured and surfaced in `doctor` (M326w) |
| §4 compaction metric | fixed (M326x); constants measured and deliberately unchanged (M326y) |
| §4 empty tool name | recorded, not acted on |
