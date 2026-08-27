# Pointing jichi at jichi: the first scored review, and two defects it found

**Date:** 2026-08-21 · **Milestone:** M515 · **Task:** run the self-hosting
pack's read-only review slice against a real diff on both a local LM Studio
server and the HRZ gateway, and score both against the pack's own rubric. ·
**Outcome:** criterion 1 is **met** on `jlu/gemma-4-26b-it` — 20 seconds, both
planted defects found with correct `file:line`, no invented nitpicks. No local
model could do it, for a measurable reason. The run surfaced a real defect in
jichi (the rules block is truncated mid-character), and the setup surfaced a
second: **77% of `CLAUDE.md` cannot reach any model.** One false result was
produced and discarded along the way; §3 is that story, because it is the most
useful part.

---

## 0. The experiment, and why it was designed this way

The pack's promotion bar asks whether a model produces "real `file:line` issues,
not invented nitpicks, not missed violations". A review of an arbitrary diff
cannot answer that: without knowing what is wrong, a plausible review and a
correct one read identically.

So the target was built with **two known defects planted in a real diff**:

| Planted | Provenance | Why this one |
| --- | --- | --- |
| `gettimeofday()` (CLOCK_REALTIME) where the deadline is armed from `jc_now_millis()` (CLOCK_MONOTONIC) | M507b, reversed | a real defect this project shipped and then fixed; subtle, and the kind a reviewer must reason about rather than pattern-match |
| a `// comment` in a C89 file | the house rule | the most basic rule in `CONTRIBUTING.md`; a reviewer that misses it is not reviewing C89 |

Both live in `tests/test_app.c`, in a 42-line working diff, in a throwaway
`git worktree` — so the primary checkout was never touched and the review had
something real to read.

## 1. Which models can review at all

`doctor --live` answers this in seconds, and it decided the experiment before a
single review ran. These reviewers work by **reading files**; a model that
cannot emit a native tool call cannot review anything — it will answer fluently
about a diff it never opened.

| Model | Endpoint | `--live` tool calling | Can review? |
| --- | --- | --- | --- |
| `qwen/qwen2.5-coder-14b` | LM Studio, LAN | observed **`text`** (configured native) | no |
| `prism-ml/bonsai-27b` | LM Studio, LAN | probe **failed** | no |
| `jlu/gemma-4-26b-it` | HRZ gateway | observed **`native`** | **yes** |

That is a finding about **this LM Studio deployment**, not about those models in
principle. It is also the single check the local config's comment block insists
on, now earning its place.

**Corrected the same day, by probing further (§8).** "Probe failed" is not a
capability verdict — it conflates *cannot call tools* with *cannot be loaded*, and
here it was mostly the latter. Asking the server directly: of the eight ids
`/v1/models` advertises, **four fail to load at all** (`qwen3.5-9b`,
`gemma-4-12b`, `bonsai-27b`, `muse-glimmer` — all
`"Failed to load model"`), and the **two loadable chat models emit tool calls as
prose the server does not translate**:

```
finish_reason: stop      tool_calls: []
content: <response>{ "name": "read_file", "arguments": { "path": "notes.txt" } }</response>
```

The model knows the schema and wants the tool; the server is not converting that
into the OpenAI `tool_calls` field. So the honest conclusion is **a deployment
finding in two parts — an over-advertised model list, and untranslated tool
calls — not a statement about what these models can do.** A local reviewer is
plausibly one LM Studio repair away, which is a much better answer than "no local
model can review", and I had published the worse one.

**Corrected once more, from the server's own logs (M518) — and this is the last
layer.** "Fails to load" was still a symptom. `~/.lmstudio/server-logs/` says it
plainly:

```
error loading model: unable to allocate ROCm0 buffer
failed to allocate buffer for kv cache
```

That is **GPU memory**, not a broken model. Measured on the host: a **16,368 MB**
card with **12,909 MB already in use**, because `lms ps` shows
`qwen2.5-coder-14b` resident at **8.99 GB** — `IDLE`, with **no TTL**, so it is
never evicted — plus the embedding model. A 9B Q4_K_M needs ~5.5 GB against
~3.4 GB free, so every JIT load of a second model fails to allocate. The four
"unloadable" models are fine. **The card is full and nothing gives it back.**

It also explains the M459 record of `qwen3.5-9b` reporting *native* on this same
server: it was the resident model then. And it changes the advice from "reinstall
the models" to **manage the memory budget** — set a TTL so idle models unload,
load one deliberately, or choose quants small enough that two fit.

**Four diagnoses, each one layer deeper, each making the last one wrong:**

| Layer I was standing on | What it told me | True? |
| --- | --- | --- |
| my own `--deadline` firing | "the gateway is too slow for review" | no — I had not closed stdin |
| `doctor --live`'s verdict | "no local model can call tools" | no — most would not load |
| the HTTP API's error | "four models fail to load" | no — a full GPU reports that way |
| the server's log + `lms ps` | "the card is full and nothing evicts" | yes, and it names the fix |

**Corrected at M519, and the correction is a fifth layer.** The row above saying
`doctor --live`'s "no local model can call tools" was false *because the models
would not load* is only half right, and the probe that "settled" it was itself
broken: its native-tool-call pattern could not cross a newline in LM Studio's
pretty-printed JSON, so it reported `prose` for **five natively capable models**.
The measured truth is that five of six emit native tool calls, and the one that
does not is the model `doctor --live` had been probing all along -- so **that
verdict was correct on the fact** and every layer above it, including this table,
was wrong about the cause. See
[`docs/analysis/2026-08-21-local-model-tool-calling.md`](2026-08-21-local-model-tool-calling.md).


The lesson is not "check twice". It is that **a symptom reported by the layer you
happen to be standing on always looks like a finding**, and the cheapest way down
a layer is usually to ask the thing itself rather than the tool wrapping it.

Two measurements taken in passing:

- **The gateway model's real context window is 196,608 tokens**, against the
  pack's conservative `contextLength: 32000`. jichi therefore compacts far
  earlier than it needs to. The config's comment said "measured honest window is
  larger"; it now has a number.
- **jichi's own `CLAUDE.md` is ~8,197 tokens of rules.** On a 16k local model
  that is half the window before the diff arrives. Self-hosting carries a context
  cost no other project has, because the rules file *is* jichi's own.

## 2. The result: criterion 1 is met

`jlu/gemma-4-26b-it`, uncapped, **20 seconds wall clock, 21,804 input tokens,
free** (a `jlu/*` model publishes no price). Verbatim:

```
### 1. c89-reviewer Findings
* MUST-FIX
  * tests/test_app.c:189: C++ style comment `//` used. C89 requires `/* ... */`.
  * tests/test_app.c:200-203: Replaced monotonic jc_now_millis() with
    gettimeofday(). This introduces non-monotonic time (CLOCK_REALTIME) into a
    test bound, which risks flakiness on systems where time can jump (e.g., NTP
    updates, system suspension). This directly contradicts the purpose of the
    assertion noted in the deleted comment.
### 2. arena-auditor Findings
* N/A (No arena mismanagement or lifetime issues detected in this diff).

Verdict: must-fixes first. (The timing regression and C89 violation must be
addressed before running `make ci`.)
```

Scored against the pack's "What good looks like":

| Criterion | Result |
| --- | --- |
| real `file:line` findings | **both**, and the line numbers are right |
| MUST-FIX separated from nice-to-have | yes, and "nice-to-have: None" stated rather than padded |
| verdict points at `make ci` | yes |
| no invented nitpicks | none |
| the arena auditor | reported **N/A** rather than manufacturing a finding — the harder half |

It also read the *deleted* comment and understood the intent behind it
("directly contradicts the purpose of the assertion noted in the deleted
comment"). That is not pattern matching.

**What this does not establish.** One diff, two planted defects, one model, one
run. It says the harness works and this model can review *this kind* of change.
It says nothing about a 500-line diff, about `src/` changes, or about the same
model on a bad day — the pack's promotion criterion 1 asks for "several real
diffs", and this is one.

## 3. The false result I nearly published

The first attempt ran with `--deadline 15m` and an outer `timeout 1200`. It
aborted after 20 minutes having produced **zero tokens and zero tool calls**
(the run journal says so: `outcome=running tokens_used=0`).

The conclusion sitting ready to write was *"the gateway is too slow for an
interactive review loop"* — which is **exactly what the pack's README already
claims**, from its own 2026-08-02 measurement. A wrong result that agrees with
the existing belief is the most dangerous kind of wrong result, and it would have
been published as a confirmation.

The elimination, in order, each step cheap:

| Suspect | Test | Verdict |
| --- | --- | --- |
| endpoint slow under load | `curl`, 30-char prompt | 1s |
| large prompts slow | `curl`, 54,000-char prompt | ~1s |
| streaming broken | `curl` with `stream: true` | streams immediately |
| streaming + tools broken | `curl` with a tools array | streams immediately |
| embeddings index build | look for `~/.jichi.d/index/` | **no index exists** |
| repo map build | `time jichi map` on this tree | **0.025s** |
| first checkpoint | `time git add -A` in the shadow repo | **0.497s** |
| jichi's request body rejected | POST a **captured real body** (18 tools, cache key, stream) from `docs/reading/traces/` | streams immediately |
| `/review-diff` specifically | a trivial prompt through jichi | **also hangs** |

The last row is the one that located it: **jichi was never sent the request at
all, because it was still reading standard input.** `tests/smoke/_smoke.sh` says
so in its own header — *"Run jichi as `$BIN` with stdin closed (`< /dev/null`) —
an open stdin on a non-TTY is read as prompt context and blocks a headless run
forever"* — a line I had read that morning while writing a different driver.

With `< /dev/null`: **0.937 seconds**, answer `OK`.

Two failures compounded here, and they are separable:

1. **The cap turned a hang into a false measurement.** Without `--deadline`, the
   run would have hung forever and been unmistakably a hang. The cap made it look
   like a *timeout*, which is a result-shaped thing, and it pointed at the
   suspect the documentation had already named.
2. **The harness error was documented and I had read the documentation.** Knowing
   a fact and applying it are different states, and the gap between them is
   invisible from inside.

The operator's rule — *measure runs before imposing any timeout or budget cap* —
is what kept this from shipping.

## 4. Defect found in jichi: the rules block is truncated mid-character

Every model call in the successful run carried this warning, six times:

```
[jichi warn] [provider] request body contained ill-formed UTF-8; replaced with
U+FFFD (a tool result or prompt section was likely cut mid-character)
```

Root-caused. The 32k-window system prompt is invalid UTF-8 at byte 35408:

```
b'`jc_tool_allow_intersect`) \xe2\x80\n... [rules truncated]'
```

`\xe2\x80` is the first **two** bytes of a three-byte character (an em dash,
U+2014 = `\xe2\x80\x94`). `src/chat/jc_rules.c:add_file` caps the combined rules
block at `JC_RULES_MAX` (32 KB) with a plain byte copy:

```c
if (c->sb.len + len > JC_RULES_MAX) {
    jc_size rem = (c->sb.len < JC_RULES_MAX) ? (JC_RULES_MAX - c->sb.len) : 0;
    jc_sb_append_n(&c->sb, data, rem);
    jc_sb_append(&c->sb, "\n... [rules truncated]");
```

**There are two truncation paths and only one is UTF-8 aware.** The
context-*fit* truncation (`[... instructions truncated to fit the model context
window ...]`) lands on character boundaries — verified on the 16k prompt, which
is clean. The 32 KB *cap* does not. That is why the defect appears only at
certain window sizes: on a 16k model the aware cut fires first and the byte cut
never happens.

**Why nobody saw it:** the provider layer detects the bad bytes, substitutes
U+FFFD and warns, so the run works. A self-healing defect with a warning nobody
reads is invisible until something prints the warning six times in a row — which
is what a self-hosting run did.

The fix is three lines and the helper already exists: back `rem` off to a
character boundary with `jc_utf8_prev()` (`include/jc_utf8.h`), which the TUI
already uses for exactly this. **Not applied in this milestone** — it is a change
to `src/`, and a defect this cheap to fix deserves a unit test proving a
mid-character cap yields valid UTF-8, which is its own small piece of work.

## 5. Defect found in the documentation: 77% of CLAUDE.md cannot reach a model

Measured with the tools jichi ships (`sysmsg`, `context`):

| Model window | Rules delivered | Share of the 139,403-byte file |
| --- | --- | --- |
| 16,384 (local) | ~4,932 tok ≈ 19.7 KB | **14.1%** |
| 32,000 (gateway as declared) | ~8,197 tok ≈ 32.8 KB | 23.5% |
| 196,608 (the real window) | ~8,197 tok — **unchanged** | 23.5% |

The second ceiling is `JC_RULES_MAX`, and it is absolute: **no model, at any
window size, can receive more than 32 KB of the rules.** The file is 139 KB.

By section:

| Section | Bytes | Reaches a model? |
| --- | --- | --- |
| preamble · What this is · **Models: local only** · Build & test | 6,742 | yes |
| **Architecture** | **116,233** | cut mid-section |
| **Tests** (test-integrity rules) | 8,902 | **no** |
| **Platforms** ("read this before a portability change") | 4,182 | **no** |
| Roadmap · Repository · Anecdotes | 2,365 | **no** |

One section is **84% of the file**. Because truncation is tail-first, *which
rules apply is decided by their line number*: the spending rule survives (it is
early), and the portability rules whose own heading says "read this before a
portability change" do not.

Design proposal, with options and their costs:
[`docs/proposals/2026-08-rules-file-split.md`](../proposals/2026-08-rules-file-split.md).

## 6. Recommendations

1. **Fix the mid-character cap** (`jc_rules.c`, three lines + a unit test).
   Cheap, and it fires on every request of every run in this repository.
2. **Split the rules file** — the proposal's Option C+E. The measurement says
   this is not tidiness: three whole rule sections currently reach nothing.
3. **Lint the rules budget.** Truncation is silent; a build failure when the
   rules block would exceed the cap turns a 77% loss into a red gate. This is the
   same move as every other lint this week, and the absence of it is why a 139 KB
   rules file went unremarked.
4. **Declare the gateway model's real window** (196,608) in the pack's config, or
   state why 32,000 is deliberate. Compaction is currently triggered ~6x earlier
   than needed.
5. **Measurement runs get no caps but `connect`.** A connect timeout can only
   fire when nothing is listening; every other bound can turn a hang into a
   plausible-looking result. §3 is the evidence.
6. **Criterion 1 needs more diffs** before the write slice is reasonable: this is
   one 42-line diff with planted defects. A `src/` diff and a large diff are the
   obvious next two.

## 7. Residue

- The local models were tested **as served by this LM Studio instance**. Whether
  a different template or a `toolCalling` setting would make `qwen2.5-coder-14b`
  emit native calls is untested, and it is the difference between "no local
  reviewer" and "no local reviewer *yet*".
- `gemma-4-12b` and `qwen3.5-9b` were probed but their verdict lines were not
  captured cleanly; only `bonsai-27b` and `qwen2.5-coder-14b` have recorded
  verdicts. Two of the eight local models are therefore untested, not failed.
- The 20-second figure is one run on an unloaded gateway. The pack's own
  measurement found the same endpoint intermittent; one fast run does not refute
  a report of variance, it just fails to reproduce it today.
- Nothing was written to the primary checkout: the review ran in a
  `git worktree`, read-only agents, `pathFence` on.
