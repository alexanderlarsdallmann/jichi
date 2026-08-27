# 追跡（ついせき）*Tsuiseki* — the traced run

*The third of the jichi source reading guides. The beginner edition is
[案内（あんない）*Annai* — the guided tour](ANNAI.md); the expert edition is
[深掘り（ふかぼり）*Fukabori* — the deep dive](FUKABORI.md). Design and scope:
[`docs/plans/2026-08-reading-guides.md`](../plans/2026-08-reading-guides.md)
for the first two, [`docs/plans/2026-08-trace-chapters.md`](../plans/2026-08-trace-chapters.md)
for this one.*

The other two guides start from the code. This one starts from **a run that
happened** — its event stream, the bytes it put on the wire, the file it left
on disk — and works back to the code that produced each of them.

That is the direction you actually work in. A bug report, a log line, a
support question, a diff someone else wrote: you are handed an *effect* and
have to find the *cause*. Reading the source top-down is how you learn a
system once; tracing an effect backwards is what you do every week
afterwards, and it is a separate skill with its own moves — narrow the
window, find the instrument, read the value, name the branch that produced
it, and know which of the numbers in front of you are the system's and which
are your instrument's.

## The contract: every trace here is replayed, not described

Prose about code rots. Prose about *a run* rots faster, because it commits to
values as well as to control flow — and a stale trace is the most convincing
wrong page in a repository, since every function it names still exists.

So no chapter here describes a run from reading the source. Each one
**replays** a recorded run and quotes the artifacts:

| | |
|---|---|
| the model | [`tests/tools/mockmodel.c`](../../tests/tools/mockmodel.c) with a scripted reply table — no key, no network, no tokens spent |
| the runner | [`docs/reading/traces/capture.sh`](traces/capture.sh) — the same script the chapter tells you to run |
| the record | `expected/` beside each trace: the event stream, every HTTP request, the workspace afterwards |
| the gate | `tests/smoke/reading_trace.sh` re-takes the trace on every `make smoke` and diffs it; `tests/smoke/reading_quotes_lint.sh` holds every quoted block in this directory to its source file |

If a chapter drifts from the code, the build goes red. That is the house rule
— prefer a lint to an audit, [`docs/TEST_INTEGRITY.md`](../TEST_INTEGRITY.md)
— applied to the one kind of documentation that cannot be checked by reading
it twice. What no lint can check is whether the *argument* around a quote is
still true; [`docs/reading/traces/README.md`](traces/README.md) says exactly
which parts of a trace are jichi's behaviour and which are the fixture's.

## Who this is for

Anyone who has been through [Annai](ANNAI.md) chapters 1–5, or who already
knows the architecture and wants to see it *actually execute*. The C is
quoted rather than pointed at, so you do not need the tree open to follow the
prose — but you will get much more out of it with the tree open, and the
exercises assume a built binary.

## Before you start: where to type, and what to have open

### The bench these chapters need

```sh
# in the jichi checkout
make && make smoke-tools
```

Both halves matter. `make` builds `./jichi`; `make smoke-tools` builds the
four C89 test helpers in `tests/tools/`, and the traces need two of them —
`mockmodel` (the scripted model) and `jsonq` (a dot-path JSON reader). A
trace run with no `mockmodel` stops with a message telling you this.

### You will be working in two places

| Place | What belongs there | How the chapters mark it |
|---|---|---|
| **The jichi checkout** — the directory you cloned and ran `make` in | everything in these chapters: `make`, `./jichi`, `sh docs/reading/traces/capture.sh`, relative paths into `src/` and `tests/` | `# in the jichi checkout` |
| **A throwaway directory** — created *for* you, one per capture, under `$TMPDIR` | the workspace the traced run edits; `capture.sh` makes it, seeds it, and deletes it | you never `cd` there; the artifacts are copied out |

**Never point a traced run at your own project.** You do not have to
remember this: unlike the exercises in the other two guides, the workspace
here is not yours to choose — `capture.sh` creates a throwaway directory,
seeds it from the trace's `trace.sh`, and removes it when the run ends. What
survives is the artifacts it copied out. The one thing to keep straight is
that the *output* directory you name is yours: `capture.sh tool-round /tmp/x`
writes into `/tmp/x`, so do not name a directory you care about.

### `jichi` or `./jichi`?

- **`./jichi`** — the binary in the checkout. What `capture.sh` runs, and what
  the gate checks. Always right after `make`.
- **`jichi`** — whatever is on your `$PATH` (after `make install`). These
  chapters never use it, because a trace has to name *which* build it traced.
  Override with `JC_TRACE_BIN=/path/to/jichi` if you mean a different one.

### Have the source open — this is a reading guide, not a manual

The chapters point at code **9 times** in the form `file.c:function_name`,
and quote it in tagged blocks besides. Line numbers are never used — they rot,
and `tests/smoke/reading_refs_lint.sh` enforces their absence — so navigate by
name: `grep -n jc_history_add_tool_result src/chat/jc_message.c`, or
`less +/jc_history_add_tool_result src/chat/jc_message.c` to open straight at
it.

Reading with **no bench at all** works better here than in the other guides:
the artifacts are committed, so you can read `expected/stdout.jsonl` in a
browser and follow every chapter without running anything. The Annai's
[Appendix A](annai-a-no-bench.md) covers the rest of that case.

## Chapters

| # | Chapter | Trace | What it traces |
|---|---|---|---|
| 1 | [A tool round, byte by byte](tsuiseki-01-a-tool-round.md) | `tool-round` | two tool calls; a result becoming input; errors as values |
| 2 | [The turn that calls no tool](tsuiseki-02-the-turn-that-calls-no-tool.md) | `plain-turn` | the shortest path through the loop, and what a turn costs before anyone decides anything |
| 3 | [The run that never reaches the network](tsuiseki-03-the-run-that-never-reaches-the-network.md) | `no-network` | a whole run with no provider; one string, two lifetimes; which runs can be recorded at all |
| 4 | [The call that was wrong, and the answer that lied](tsuiseki-04-the-call-that-was-wrong.md) | `wrong-args` | a run whose summary is identical to the successful one, and whose answer is false |

All four ship (M508–M509), and every trace in the table is replayed and
diffed by `tests/smoke/reading_trace.sh` on each `make smoke`.

**Read 1 and 2 as a pair.** They are the same command against the same
workspace; the difference between their artifacts is the entire cost of a tool
call. **Read 1 and 4 as a pair too**: same prompt, same workspace, one field
different in the model's second call — and the two runs' summary events are
byte-identical apart from the sentence the model wrote. If you read only one
section of this guide, make it chapter 4's finding.

Chapter 3 is the odd one out on purpose. It traces a run with no model in it,
and it is where the series states its own limit: a run can be recorded when
its output depends only on its input, which rules out `doctor` (machine facts,
and a reachability probe) and everything else that reports the world it ran in.

**After the traces, the rung above reading.** Read it, trace it, then *drive it
on itself*: [`docs/READING_OPEN_SOURCE.md`](../READING_OPEN_SOURCE.md) §"Then
drive it on itself" sequences that, and `examples/self-hosting/README.md` is the
pack — read-only first, no institutional key needed.

**A fifth chapter is not planned.** Four shapes of run — tool round, no tool,
no provider, wrong call — cover the ground this guide set out to cover. What
comes next is a trace *you* record: `docs/reading/traces/README.md` is the
procedure, and the same two gates will hold it.
