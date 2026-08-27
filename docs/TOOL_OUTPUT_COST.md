# What tool output costs, and how to spend less

> **Prerequisite:** none, but this reads best after
> [COMPACTION.md](COMPACTION.md) (what jichi does when the history gets too big)
> and [PROMPT_CACHING.md](PROMPT_CACHING.md) (what a cache would do about it).

A tool result is not paid for once. It joins the conversation, and on a backend
without prompt caching it is re-sent — and re-billed — on **every subsequent
model call in that turn**. This page is about that multiplier: where it actually
comes from, what jichi already does about it, why compaction is the wrong place
to fix it, and which levers are worth pulling.

Everything here is measured. The figures come from a **private downstream
workspace's** telemetry, 36,925 events over 11 days, stamped **2026-08-07** (full
analysis in [`analysis/2026-08-07-downstream-telemetry.md`](analysis/2026-08-07-downstream-telemetry.md)).
They describe one real workload, not a universal law — the *shape* generalises,
the numbers are that project's.

| | section |
| --- | --- |
| §1 | [The multiplier](#1-the-multiplier) |
| §2 | [Where the bulk actually is](#2-where-the-bulk-actually-is) |
| §3 | [What jichi already does](#3-what-jichi-already-does) |
| §4 | [Why compaction cannot fix it](#4-why-compaction-cannot-fix-it) |
| §5 | [The levers](#5-the-levers) |
| §6 | [Recommendations](#6-recommendations) |
| §7 | [Design decisions](#7-design-decisions) |
| §8 | [Measure your own](#8-measure-your-own) |

---


> **A pattern worth considering, from a comparable tool.** opencode keeps a bounded preview of
> oversized tool output in history and moves the **complete text to a temp file whose path it
> gives the model**, rather than discarding the remainder as jichi's caps do. That trades a
> truncation the model may re-run the command to recover for a second read it can choose. Which
> is cheaper on a backend with no prompt caching is measurable and unmeasured. Analysis, with
> two other ideas worth taking and an honest account of a grep-based first pass that got them
> wrong: [analysis/2026-08-09-opencode-continue-comparison.md](analysis/2026-08-09-opencode-continue-comparison.md).

## 1. The multiplier

Within one turn the agent calls a tool, appends the result to the history, and
calls the model again. The history only grows. So a result produced at call *n*
of a turn that runs *N* calls is sent **N − n** more times.

With prompt caching that is nearly free after the first send. Without it, it is
billed in full every time. In the measured workload:

| | |
| --- | --- |
| model calls per turn | median **26**, p90 115, max **458** |
| prompt cache hit-rate | **0%** across 1.24 billion input tokens |
| input:output ratio | **416:1** |

A 50 KB read at call 10 of a 256-call turn is re-sent ~246 times: **one tool call
responsible for roughly 12 MB of billed input.** That is the number to keep in
mind. It is also why the advice below is about *not producing* the output rather
than about removing it afterwards.

---

## 2. Where the bulk actually is

**18.0 MB of tool output across 14,988 calls**, and it is extremely concentrated:

| | share of all bytes |
| --- | --- |
| top **1%** of calls (149) | **27%** |
| top 5% (749) | 53% |
| top 10% | 67% |
| top 25% | 85% |

You do not need broad discipline. You need those few calls.

### 2.1 Whole-file reads — 36% of all output

| | |
| --- | --- |
| `read_file` calls returning over 8 KB | **344** |
| …of which used `offset` / `limit` | **12** |
| bytes in those calls | **6.4 MB (36% of all tool output)** |

`read_file` has taken `offset` and `limit` since M8. They were used in 3% of the
calls where they would have mattered most.

### 2.2 Re-reading the same file — 72% of reads

| | |
| --- | --- |
| `read_file` calls | 2,056 |
| distinct paths | **584** |
| re-reads (beyond the first per path) | **1,472 (72%)** |
| most-read single path | **216 times** |

This is the one that should be surprising. It is also a *loop*: compaction elides
an old read, the model no longer sees the file, it reads it again, compaction
elides it again.

### 2.3 The shell is volume, not size

| | |
| --- | --- |
| `run_terminal_command` calls | **8,530** |
| median output | **195 bytes** |
| total | 4.7 MB |

Individually tiny. The cost is the 8,530 — each one is a full model round-trip
re-sending the whole prompt. Capping shell output would barely touch this;
*making fewer calls* would.

### 2.4 By tool

| tool | total | calls | mean |
| --- | --- | --- | --- |
| `read_file` | 10.3 MB | 2,056 | 5,033 B |
| `run_terminal_command` | 4.7 MB | 8,530 | 556 B |
| `edit_file` | 1.4 MB | 1,753 | 788 B |
| `search_code` | 0.5 MB | **174** | 3,123 B |

`search_code` is the striking row: 174 calls against 2,056 reads. It is the tool
that finds *which lines matter* so a read can be bounded, and it is barely used.

---

## 3. What jichi already does

Four mechanisms, none of which is a substitute for producing less:

- **Per-tool output caps** (§5) truncate a single result. A safety bound against
  one runaway command, not a budget.
- **Superseded-read dedup** (M93/M94) drops a `read_file` result once a later
  read of the same path lands — zero-loss, and it runs every round rather than
  waiting for pressure, precisely because on a cacheless backend a duplicate is
  re-billed until it goes. In the measured workload it made 480 elisions against
  1,472 re-reads: it catches roughly a third, because it can only remove a copy
  that has been *superseded*, and the last six messages are protected.
- **Mid-turn elision** ([COMPACTION.md](COMPACTION.md)) trims old tool results
  under pressure. See §4 for why this is not the answer.
- **`--lite`** ships much tighter caps (§5) for low-RAM machines.

**The dedup never prevents the first send.** Nothing can: jichi cannot know a
result is redundant until it has it.

---

## 4. Why compaction cannot fix it

Measured across 593 pressured mid-turn passes, reclaim decays *within* a turn:

| pass # in turn | n | median tokens reclaimed |
| --- | --- | --- |
| 1st | 103 | **10,324** |
| 2nd | 51 | 1,306 |
| 3rd | 36 | −3 |
| 26+ | **174** | −98 |

The first pass harvests everything eligible. Elided content shrinks to ~660 bytes
— under `ELIDE_MIN_BYTES` (800), and the idempotence guard never re-elides it —
while newer results are protected by `MIDTURN_KEEP_RECENT` (6). By the third pass
there is nothing left, and **174 of 593 pressured passes were the 26th or later
in their turn**, each scanning the whole history to reclaim nothing.

Material is not the constraint: **86% of tool-output bytes already exceed the
800-byte elision floor.** Lowering the floor to 200 would reach 98% of bytes — 12
points, for 2.2× the elision churn.

**And compaction is always too late.** It can only remove a result *after* it has
been sent and billed at least once — usually many times, since the trigger fires
at 80% of the limit and the result has been riding along since it was produced.
That is the whole argument of this page: the cheap fix is upstream.

M326y's `unrelieved` flag marks the turns where eliding has stopped helping at
all.

---

## 5. The levers

### 5.1 Output caps

Every tool's result is truncated at a cap. The built-in is a safety bound; the
`--lite` column is what jichi uses on a low-RAM machine; the config key overrides
both.

| tool | built-in | `--lite` | config key |
| --- | --- | --- | --- |
| `read_file` | **256 KB** | 64 KB | `readMaxBytes` |
| `run_terminal_command` | 64 KB | 16 KB | `runMaxBytes` |
| `search_code` | 64 KB | 16 KB | `searchMaxBytes` |
| `fetch_url` | 128 KB | 32 KB | `fetchMaxBytes` |
| `git_*` | 32 KB | 8 KB | `gitMaxBytes` |

A cap of `0` means "use the built-in". These are checked against the source by
`tests/smoke/tool_caps_lint.sh`, so the table cannot drift.

**A capped `read_file` still knows the whole file** (M594). Only the *output* is
bounded — the file is read in full — so the truncation notice reports both
numbers, and a range beyond the returned window is explained rather than denied:

```
(no lines in range; the file has 12509 lines but only the first 4027 were read
 -- readMaxBytes is 262144; raise it, or read within line 1-4027)
... [output truncated] (read lines 1-4027 of 12509; readMaxBytes=262144)
```

Before M594 that first message read *"file has 4027 lines"*, which is the count of
what was **returned**. A model asked to find something at line 11,715 was told the
file ends at 4,027 and stopped looking — a cap presented as the end of the file.
PDFs are the one case where the true count is unknown, because the extractor
itself is bounded; there the message says so instead of inventing a number.

**`--lite`'s caps exist for RAM, but they answer a different question equally
well.** A cacheless backend has nothing to do with memory pressure, and yet wants
the same numbers. That connection is not made anywhere in jichi today — see §7.

### 5.2 Bounded reads

`read_file` takes `offset` and `limit`. `search_code` takes `context`. Together
they turn "show me this 60 KB file" into "show me the 40 lines around the thing I
asked about". This is the single largest measured lever and it needs no
configuration at all.

### 5.3 Fewer calls

8,530 shell calls is 8,530 round-trips, each re-sending the full prompt. Batching
(`&&`, a script, `apply_patch` instead of several `edit_file`s) reduces the
multiplier itself rather than the thing being multiplied.

---

## 6. Recommendations

**For an operator, in order of measured effect:**

1. **Set `readMaxBytes`.** 256 KB is a safety bound, not a budget. On a cacheless
   backend, 32–64 KB is defensible and costs you a truncation notice on files
   that were never going to be read in full anyway.
2. **Check whether your backend caches.** `jichi doctor` says so since M326w, and
   `jichi telemetry --cache-audit` gives the breakdown. If it caches, most of this
   page is a rounding error. If it does not, §1's multiplier applies to every byte.
3. **Consider `--lite`'s caps without `--lite`.** The five keys above give you the
   tighter numbers without the other resource trade-offs (snapshots off, no repo
   map, parallel 1).
4. **Watch `unrelieved` in telemetry.** It names the turns where eliding has
   stopped helping; those are the ones to shorten.

**For an agent's instructions — now built in (M440).** Items 5–8 below were advice
addressed to a *human*, to be copied by hand into an `AGENTS.md`, while jichi knew
every input at runtime. They are now a `# Cost model` system-prompt section:

```
# Cost model

Every tool result you receive stays in the conversation and is re-sent with every
later request in this turn. Output you take once is therefore billed many times,
and this run is not reusing a cached prompt prefix, so that applies to every byte.

Your tool output is capped at: read_file 64 KB, run_terminal_command 16 KB,
fetch_url 32 KB, search_code 16 KB, the git tools 8 KB. …
```

The caps are the **effective** ones (`jc_config_cap` over the built-in defaults in
`include/jc_toolcaps.h`), so a `--lite` or hand-tightened run reports the numbers it
will actually enforce — the point being that a model can then predict a truncation
instead of paying for one.

**Gating: `costModel` (config), `--cost-model` / `--no-cost-model`.** Tri-state,
default **auto**, which emits the section only when prompt caching is **off** for the
active model. That is the whole of §1's argument turned into a rule: on a cacheless
backend the multiplier applies to every byte and the advice pays for itself many times
over; with a cached prefix the same prose is billed once and buys little. Unconditional
frugality prose was *rejected* — it is wrong on one of the two backend classes.

**The known gap, and what to do about it.** The gate reads the *configured* cache
setting, not the observed hit-rate. It has to: the observed rate is a running
statistic, and putting it in the system prompt would change the cached prefix from turn
to turn and destroy the very caching it describes (see
[PROMPT_CACHING.md](PROMPT_CACHING.md)). So a backend that **accepts a caching request
and returns no cached tokens** — a measured case — gets no section by default. That is
what `doctor` and `telemetry --cache-audit` are for, and what the explicit
`costModel: true` / `--cost-model` is for once they tell you.

This does **not** re-open §7's rejection of auto-bounding reads. The section informs the
model's decision; it never narrows the answer to an explicit request, and it never tells
the model to refuse one.

**For an agent's instructions (`AGENTS.md`, a skill, or an output style):**

5. **Search before reading.** 174 `search_code` calls against 2,056 reads is
   backwards. Finding the lines first is what makes a bounded read possible.
6. **Read a range, not a file.** Name `offset` and `limit` whenever the file is
   large and the question is local.
7. **Do not re-read what is already in the conversation.** A 72% re-read rate is
   the agent not trusting its own context. If a file was elided and is genuinely
   needed again, re-read a *range*.
8. **Batch shell work.** One command that does three things costs one round-trip;
   three commands cost three, each carrying the entire prompt.

**What we are deliberately not recommending:** lowering `ELIDE_MIN_BYTES` or the
80/60 compaction thresholds. Both were measured and neither is the constraint
(§4).

---

## 7. Design decisions

**The built-in caps are not lowered.** They are a *safety bound* — the thing that
stops one runaway command from exhausting memory — and a safety bound that also
tries to be a token budget serves neither well. A 256 KB read cap is right for
"do not die"; 32 KB is right for "do not overspend", and only the operator knows
which backend they are on. *Rejected:* shipping the `--lite` numbers as the
default, which would silently truncate reads for everyone including users on a
caching backend where the cost does not exist.

**jichi does not auto-bound reads.** It could refuse a whole-file read over some
size, or inject `limit` when the model omits it. *Rejected:* the model asked for
the file, and a silently partial answer to an explicit request is the kind of
help that produces a wrong conclusion two turns later. The cap already truncates
*with a notice*; that is the honest version of the same idea.

**The fix is not in compaction, and that is a measurement, not a preference.**
See §4: reclaim decays to zero by the third pass, and no threshold changes that.
Recorded so nobody re-opens it with the intuitive-but-wrong "just compact
sooner".

**`--lite`'s caps are not automatically applied on a cacheless backend.** It is
tempting: jichi *knows* the hit-rate after a few calls (M326w), and could tighten
the caps itself. *Rejected for now:* it would change tool behaviour mid-session
based on a statistic, which is hard to predict and harder to debug — a read that
returned 200 KB yesterday returning 32 KB today, for reasons invisible in the
config. `doctor` advises instead. Recorded in [DEFERRED.md](DEFERRED.md).

**This page reports one workload and says so.** *Rejected:* presenting the
figures as general. The concentration (top 1% of calls = 27% of bytes) and the
compaction decay are structural and should reproduce; the 72% re-read rate is a
fact about one agent on one codebase.

---

## 8. Measure your own

Telemetry is off by default. Turn it on, work for a few days, then look:

```sh
jichi --config ~/.jichi --config-editable config telemetry metrics
jichi doctor                      # does your backend cache? (M326w)
jichi telemetry --cache-audit     # the per-model and per-session breakdown
jichi telemetry                   # compaction pressure, unrelieved, tool ok-rates
jichi context tools               # what your tool definitions cost per call
```

The three numbers worth writing down: your **cache hit-rate** (0% means §1
applies), your **model calls per turn** (the multiplier), and your
**`unrelieved`** share (turns eliding can no longer save).

---

**See also:** [COMPACTION.md](COMPACTION.md) ·
[PROMPT_CACHING.md](PROMPT_CACHING.md) · [DOCTOR.md](DOCTOR.md) ·
[LOW_MEMORY.md](LOW_MEMORY.md) · [OBSERVABILITY.md](OBSERVABILITY.md)

## This page assumes an uncached backend, and says so (M341)

Everything above is measured against a backend that returns **zero** cached tokens, where every
token of the prefix is billed on every call. That is the configuration jichi is used in here, so
this advice stays primary.

**On a caching backend it inverts.** A large prefix is billed once at ~1.25x and then read back at
~0.10x, so trimming it saves almost nothing -- and trimming it *below the model's minimum
cacheable block* silently disables caching entirely and costs you everything. Measured minimums
are 4096 tokens for Haiku 4.5 and Opus 4.5, 1024 for Sonnet 4.5;
`doctor` warns when a configured prefix is under the line.

So: trim the prefix when nothing caches, and grow it when something does. Check which you have
before applying either -- `doctor` reports it, and
[analysis/2026-08-09-hrz-prompt-caching.md](analysis/2026-08-09-hrz-prompt-caching.md) has the
measurement.