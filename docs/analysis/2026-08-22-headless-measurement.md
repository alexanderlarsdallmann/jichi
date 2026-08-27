# Driving jichi headless against four real projects: what it measured

*2026-08-22 (M540). Four read-only agent runs on a local 9B model, with telemetry
and the run journal captured. Every number below came out of the files named; the
raw artifacts are not committed (they contain absolute paths from this machine),
but every command is reproducible from what is here.*

## The rig

| | |
|---|---|
| Model | `qwen/qwen3.5-9b` via LM Studio, 131,072 context, 2 parallel slots, 1h TTL |
| Embedding | `text-embedding-nomic-embed-text-v1.5` (loaded; unused by these runs) |
| Endpoint | `http://192.168.0.24:1234/v1` — **not** loopback, see below |
| Fences | `--readonly`, `pathFence: true` |
| Caps | none: no `--deadline`, no `--budget-tokens`, `timeouts.stall` raised to 1800 |
| Capture | `--output jsonl`, `--log … --log-level metrics`, `--journal …` |
| Prompt | *"Describe this project for a newcomer: what it builds, its main modules or directories, how it is tested, and one thing you would improve. Read the files you need to answer accurately — do not guess."* |

Fences on and caps off is the rule from `CLAUDE.md`, and both halves earned their
place in this run — see §3 and §4.

## 1. The results

| Project | rc | wall | model calls | in tok | out tok | tools | peak input | verdict |
|---|---|---|---|---|---|---|---|---|
| chrtext | 0 | 50s | 7 | 148,709 | 1,625 | 13 | 23,910 | good answer |
| zigodot | 0 | 52s | 6 | 208,082 | 912 | 10 | 42,430 | **preamble only** |
| basicfantasy | 0 | 57s | 11 | 265,905 | 1,707 | 18 | 33,254 | good answer |
| jichi | 1 | 84s | — | 14,434 | 131 | 3 | 14,434 | **stall timeout** |
| jichi (caps off) | 0 | 119s | 5 | 203,288 | 557 | 5 | 94,893 | **empty answer** |
| jichi (repeat) | 0 | 150s | 7 | — | — | 7 | 100,717 | good answer, 4,505 chars |

Latency, from the telemetry:

| Project | min | median | max | output throughput |
|---|---|---|---|---|
| chrtext | 1.3s | 3.2s | 16.9s | 32.6 tok/s |
| zigodot | 2.0s | 10.0s | 13.8s | 17.6 tok/s |
| basicfantasy | 1.1s | 3.3s | 14.7s | 29.9 tok/s |

Tool mix: `read_file` and `list_files` dominate everywhere. basicfantasy also drew
one `run_terminal_command` and one `run_tests`.

**`in tok` is cumulative across the turn's calls, not the context size.** chrtext's
148,709 input tokens over 7 calls against a 23,910-token peak is the tool loop
re-sending a growing history each iteration — the cost model
[`TOOL_OUTPUT_COST.md`](../TOOL_OUTPUT_COST.md) describes, visible in real numbers.

## 2. `--readonly` does not stop the shell

basicfantasy's run executed `run_terminal_command` and `run_tests` **under
`--readonly`**. That is correct behaviour and worth stating plainly: `--readonly`
disables *mutating* tools; it does not forbid command execution. The flag that
forbids the shell is `--strict-scope`, and it only applies alongside
`--edit-scope`.

An operator who reads `--readonly` as "cannot affect anything" is wrong, and until
M539 could not have learned otherwise from the man page. If you need "no writes and
no shell", you need both flags.

## 3. A cap fired, and would have been published as a model property

The first jichi run failed at **84 seconds** with `stop_reason: timeout`, error
*"model stalled (timed out)"* — the default stall timeout, on a 9B reading a large
repository. With `timeouts.stall` raised to 1800s the same prompt completed in
**119 seconds**.

Had the first number been reported, it would have described *my timeout* as a
property of the model. This is why measurement runs drop caps: a cap that fires does
not hide the answer, it manufactures a plausible different one.

**The finding that survives**, and it is about the default rather than the model:
jichi's default stall timeout is tuned for hosted models and is too tight for a
local 9B on a repository of this size. The default is not wrong — a stall timeout
that never fires is not a stall timeout — but a local-model config should raise it,
and [`LOCAL_MODELS.md`](../LOCAL_MODELS.md) should say so.

## 4. `-q` silenced the diagnostics that would have explained the result

The uncapped jichi run returned `rc=0`, `stop_reason: done`, 5 tool calls, 557
output tokens — and an **empty answer**. `text: ""`.

jichi has a warning for precisely that triple. M167 fires when tools were advertised
and the model returned neither a tool call nor text, and it says so *"once per
session, at any depth"*. M521's reasoning diagnostic covers the neighbouring case.
Both write to stderr.

My harness passed `-q`.

So the run that most needed explaining was the one I had configured to be silent.
`CLAUDE.md` already carries this lesson from a different direction — *"jichi does
warn, and I did not read it"* — and it cost a second reading here. **A measurement
harness must not pass `-q`.** The whole point of a measurement is the part you did
not predict, and that part arrives on stderr.

## 5. What I will not claim

**The empty answer did not reproduce.** Same prompt, same model, same config: the
next run returned a 4,505-character, accurate answer with 7 tool calls at a
100,717-token peak. One run in two at ~95–100k input produced 312 output tokens and
empty content. That is a **model-side flake at high context**, not a jichi defect,
and filing it as one would have been wrong.

**I have not observed M167's warning firing.** I established that the code path
exists and matches the condition; the failure did not recur once diagnostics were
on, so the warning was never printed in front of me. Those are different claims and
this page keeps them apart.

## 6. Two answers were not answers

`zigodot`'s complete response was:

> *"I'll read through the key documentation files to give an accurate overview of
> this project."*

`rc=0`, `stop_reason: done`, 10 tool calls, and the text is an **announcement of
intent**. The loop ended because the model emitted text without a tool call, which
jichi correctly treats as the final answer.

This is not a bug with an obvious fix — "the answer must be substantive" is not
mechanically decidable — but it is the dominant failure mode of driving a small
model headlessly, and a supervisor keying on `rc` will record it as a success.
Anyone building on `--output jsonl` should treat `done` with a short `text` and a
nonzero `tool_calls` count as suspect. jichi's own
[`hollow_green_note`](../../tests/smoke/hollow_green_note.sh) machinery makes the
analogous argument about verify commands.

By contrast `basicfantasy`'s answer was genuinely accurate — it identified the Zig
rewrite from a Python prototype, the `zig/src/{core,systems,ui,utils}` layout, the
JSON data files and the pytest suite, none of which was in the prompt.

## 7. M536 validated in the wild

The journal and telemetry rows from these runs carry the M536 stamper's output:

```json
{"name":"run_terminal_command","ok":true,"error":false,"exit":0}
```

Both sinks, both field names, the same polarity, and `exit` present on the journal
row. Before M536 the journal row read `{"name":"run_terminal_command","error":false}`
— no `ok`, no `exit` — so a supervisor joining the two sinks on `run` could not have
told a red gate from a broken tool. This is the first time that fix has been seen on
data jichi produced against a real project rather than a fixture.

## 8. The endpoint was not on loopback

All four runs failed instantly at first with `error: http error`. LM Studio reported
*"The server is running on port 1234"* — and `ss -ltnp` showed it bound to
**`192.168.0.24:1234`**, not `127.0.0.1:1234`. With "serve on local network"
enabled, the loopback bind is absent, so an `apiBase` of `http://127.0.0.1:1234/v1`
is refused.

`lms server status` reports the port and not the interface, so the obvious config is
wrong in a way the obvious diagnostic does not reveal. Check `ss -ltnp` when a local
endpoint refuses a connection that "should" work.

## Reproducing this

```sh
# 1. what the server is ACTUALLY bound to
ss -ltnp | grep lm-studio

# 2. one project, fences on, caps off, diagnostics ON (no -q)
cd /path/to/project
jichi --config measure.json --no-session --readonly --output jsonl \
      --log telem.jsonl --log-level metrics --journal journal.jsonl \
      -p 'Describe this project for a newcomer …' < /dev/null

# 3. the numbers
grep '"event":"model_call"' telem.jsonl   # latency, in_tok, out_tok, ok
grep '"event":"tool_call"'  journal.jsonl # name, ok, error, exit
```

Run each of these detached with `setsid` if you are driving them from a harness with
its own timeout: a tool timeout SIGTERMs the process group and kills the run you are
measuring.

## Where this fits

- [`docs/TESTING_RUNBOOK.md`](../TESTING_RUNBOOK.md) §8 — the fences-on / caps-off /
  diagnostics-on rule, generalised from this run.
- [`docs/LOCAL_MODELS.md`](../LOCAL_MODELS.md) — the local-model guidance the §3
  stall finding belongs in.
- [`docs/TOOL_OUTPUT_COST.md`](../TOOL_OUTPUT_COST.md) — why cumulative input tokens
  are several times the peak.
