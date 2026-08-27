# Plan: the trace chapters — 追跡（ついせき）*Tsuiseki*

*Status: COMPLETE — all four chapters shipped (M508 chapter 1, M509 chapters
2–4). This document is the executed design.
Companion surfaces: [`docs/reading/TSUISEKI.md`](../reading/TSUISEKI.md) (the
index), [`docs/plans/2026-08-reading-guides.md`](2026-08-reading-guides.md)
(the two existing guides, whose conventions this inherits),
[`docs/READING_OPEN_SOURCE.md`](../READING_OPEN_SOURCE.md) (whose method step 3
— "trace one concrete case" — this is the missing worked example of).*

## 1. Why a third guide, when two exist

The Annai and the Fukabori are both **code-first**: they hand the reader the
map (here is the system, here is where each part lives, now go run it) or the
argument (here is a decision and its alternatives). Between them they cover
the architecture well, and the honest starting point of this plan is that
roughly 60% of "explain the program flow of a run" was already shipped.

What no page in the tree did was the **inverse direction**: start from a run
that happened and work back to the code. That is the direction real work
arrives in — a bug report, a log line, a diff someone else wrote — and it is a
separate skill with its own moves (narrow the window, find the instrument,
read the value, name the branch, distinguish system from instrument).

Concretely, three things were missing and are now the remit:

1. a **specific** run with **actual values** at each hop, not a canonical one;
2. **how you find out** which branch was taken, rather than the conclusion;
3. **data flow in the C sense** — who allocated this string, which arena owns
   it, when it dies — which `fukabori-03` argues abstractly and nothing
   demonstrates.

## 2. The load-bearing decision: capture, do not compose

A trace written by reading the source is a claim. A trace taken from a run is
evidence. Prose about a run also rots faster than prose about code, because it
commits to values as well as to control flow, and a stale trace is the most
convincing wrong page in a repository: every function it names still exists.

So every chapter replays a real run, and the run is deterministic:
`tests/tools/mockmodel` with a scripted reply table, `--output jsonl`, a
throwaway workspace, no key and no network. `docs/reading/traces/capture.sh`
takes the trace; `expected/` holds the record; `tests/smoke/reading_trace.sh`
re-takes it on every `make smoke` and diffs.

**Rejected: a hand-written narrative with anchors** (the Annai's own form).
Cheaper to write, and it is what the first draft of this plan was. It fails
the M508 test case: while writing chapter 1 the fixture called `edit_file`
with `old`/`new` instead of `old_string`/`new_string`; the tool refused, the
loop carried on, and the fixture's final sentence claimed success anyway. A
composed narrative would have described the successful run the author
intended. The capture described the run that happened. That failure is now the
chapter's closing section and chapter 4's subject.

**Rejected: recording a real model's run.** More honest about model behaviour,
and impossible to hold still: no lint can diff a trace whose values change per
call, and the spend rule (`CLAUDE.md`) makes a per-`make smoke` model call a
non-starter. The cost is stated in the chapters instead — every model decision
in these traces is a fixture's decision, and nothing about model behaviour may
be concluded from them.

**Rejected: quoting nothing, anchoring everything** (the guides' existing
convention, `file.c:function`). It is the right rule for a map and the wrong
one for a trace: the reader needs the four lines that *did* the thing in front
of them. Quoted code is a new rot surface, so it comes with
`tests/smoke/reading_quotes_lint.sh`: a fence tagged with its origin file must
match that file line for line. What the lint deliberately does not check —
order, adjacency, whether the excerpt still means what the prose says — is
named in its header (the M305 rule).

## 3. Placement and conventions

- `docs/reading/tsuiseki-NN-*.md`, index `docs/reading/TSUISEKI.md`: a third
  prefix in the existing directory rather than a new tree, so
  `reading_refs_lint.sh` and `docs_locators_lint.sh` cover it from the first
  file and `docs/README.md` needs no new row.
- Artifacts under `docs/reading/traces/<name>/`, with the capturer beside
  them. `docs/` carrying an executable script has precedent
  (`docs/assignments/*/test.sh`), and the reader's reproducer being *the same
  script the gate runs* is the point: one implementation, so what you re-take
  by hand is what CI checks.
- Anchor counts are now **per series** (`reading_refs_lint.sh` check 4). The
  single global count was green while both index pages said "the chapters
  point at code 101 times" and meant the whole directory — a lint passing the
  exact defect it exists to prevent. Annai 44, Fukabori 57, Tsuiseki 7 at M508.

## 4. The chapter map

| # | Chapter | Trace | What it is for |
|---|---|---|---|
| 1 | A tool round, byte by byte | `tool-round` | the loop's second iteration; a tool result becoming input; errors as values. **M508.** |
| 2 | The turn that calls no tool | `plain-turn` | the shortest path through `run_agent_loop`; what a turn costs before anything is decided (11,635 of 14,534 bytes are tool schemas). **M509.** |
| 3 | The run that never reaches the network | `no-network` | how much of the program is not the agent loop; one string with two lifetimes; the limit of what a trace can be. **M509.** |
| 4 | The call that was wrong, and the answer that lied | `wrong-args` | error-as-value end to end, and two runs whose summary events are byte-identical while one did the work and the other did not. **M509.** |

**Chapter 3's trace is `no-network` (`jichi map`), not `doctor`, and the
substitution is a measurement.** `doctor` was the obvious candidate and fails
twice: its output reports the machine (kernel string, paths, cores, RAM), so a
byte-diff gate would fail on the next machine for a correct reason; and a plain
`doctor` run is not offline — `docs/DOCTOR.md` says it is "offline or a bare
reachability probe", and the probe shows up in the output as `✓ model server
reachable`. Both claims were checked by running the command and reading it. The
rule that fell out is now stated in the chapter: **a run can be recorded when
its output depends only on its input.** `doctor` became chapter 3's third
exercise instead, which is a better use of it.

Deferred on purpose: compaction and fork-based parallelism. Both are where a
captured trace gets expensive (two histories; a child process's artifacts), and
Fukabori 5 and 7 already argue the designs. **No fifth chapter is planned**:
four shapes of run cover the ground, and what should come next is somebody
else's recorded trace, for which `docs/reading/traces/README.md` is the
procedure.

## 5. What this does not do

It does not replace the Annai (a reader who does not know what a tool call *is*
should not start here), and it does not audit prose. The gate holds the
artifacts and the quotes; whether the argument around a quote is still true is
a reader's job — the counterweight `docs/TEST_INTEGRITY.md` states, applied to
the one documentation form whose facts are otherwise uncheckable.
